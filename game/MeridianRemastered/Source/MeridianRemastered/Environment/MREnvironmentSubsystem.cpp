#include "Environment/MREnvironmentSubsystem.h"

#include "Components/LightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/MRGameTimeSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MRPlayerState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	const TCHAR* CollectionPath = TEXT("/Game/Generated/Environment/Materials/MPC_Environment.MPC_Environment");
	const FName NightLampTag(TEXT("NightLamp"));

	TAutoConsoleVariable<float> CVarUpdateSeconds(
		TEXT("mr.Env.UpdateSeconds"), 5.f,
		TEXT("How often (real seconds) the environment director re-evaluates time of day; it applies only changes."));
	TAutoConsoleVariable<FString> CVarMood(
		TEXT("mr.Env.Mood"), TEXT(""),
		TEXT("Pin one mood from moods.json (no day/night cycle); empty follows the cycle."));
	TAutoConsoleVariable<int32> CVarEditor(
		TEXT("mr.Env.Editor"), 0,
		TEXT("1: the environment director also drives editor worlds (it edits the open level's lighting)."));

	FAutoConsoleCommandWithWorld CmdReload(
		TEXT("MREnvReload"), TEXT("Re-read data/environment/moods.json and re-apply the environment."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UMREnvironmentSubsystem* Env = World ? World->GetSubsystem<UMREnvironmentSubsystem>() : nullptr)
			{
				Env->Reload();
			}
		}));

	double Number(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double Default)
	{
		double Value = Default;
		return Obj.IsValid() && Obj->TryGetNumberField(Field, Value) ? Value : Default;
	}

	TSharedPtr<FJsonValue> CopyValue(const TSharedPtr<FJsonValue>& V);

	TSharedPtr<FJsonObject> CopyObject(const TSharedPtr<FJsonObject>& Obj)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		for (const auto& Pair : Obj->Values)
		{
			Out->SetField(Pair.Key, CopyValue(Pair.Value));
		}
		return Out;
	}

	TSharedPtr<FJsonValue> CopyValue(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid())
		{
			return V;
		}
		switch (V->Type)
		{
		case EJson::Object:
			return MakeShared<FJsonValueObject>(CopyObject(V->AsObject()));
		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const TSharedPtr<FJsonValue>& Item : V->AsArray())
			{
				Items.Add(CopyValue(Item));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		default:
			return V;  // numbers, strings, bools are immutable
		}
	}

	/** Blend two mood values: numbers and number arrays interpolate, objects blend field by field,
	    anything else switches at the midpoint. Always returns new objects. */
	TSharedPtr<FJsonValue> BlendValue(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B, double T)
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return CopyValue(A.IsValid() ? A : B);
		}
		if (A->Type == EJson::Number && B->Type == EJson::Number)
		{
			return MakeShared<FJsonValueNumber>(FMath::Lerp(A->AsNumber(), B->AsNumber(), T));
		}
		if (A->Type == EJson::Array && B->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& LA = A->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& LB = B->AsArray();
			bool bNumeric = LA.Num() == LB.Num();
			for (int32 i = 0; bNumeric && i < LA.Num(); ++i)
			{
				bNumeric = LA[i]->Type == EJson::Number && LB[i]->Type == EJson::Number;
			}
			if (bNumeric)
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (int32 i = 0; i < LA.Num(); ++i)
				{
					Out.Add(MakeShared<FJsonValueNumber>(FMath::Lerp(LA[i]->AsNumber(), LB[i]->AsNumber(), T)));
				}
				return MakeShared<FJsonValueArray>(Out);
			}
		}
		if (A->Type == EJson::Object && B->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> OA = A->AsObject();
			const TSharedPtr<FJsonObject> OB = B->AsObject();
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			for (const auto& Pair : OA->Values)
			{
				Out->SetField(Pair.Key, BlendValue(Pair.Value, OB->TryGetField(Pair.Key), T));
			}
			for (const auto& Pair : OB->Values)
			{
				if (!OA->HasField(Pair.Key))
				{
					Out->SetField(Pair.Key, CopyValue(Pair.Value));
				}
			}
			return MakeShared<FJsonValueObject>(Out);
		}
		return CopyValue(T < 0.5 ? A : B);
	}

	FString ToJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	/** moods.json uses the editor's Python names (snake_case); find the C++ property they stand for. */
	FProperty* FindProperty(const UStruct* Struct, const FString& Snake)
	{
		TArray<FString> Parts;
		Snake.ParseIntoArray(Parts, TEXT("_"));
		FString Pascal;
		for (const FString& Part : Parts)
		{
			Pascal += Part.Left(1).ToUpper() + Part.Mid(1);
		}
		for (const FString& Name : {Pascal, TEXT("b") + Pascal})
		{
			if (FProperty* Property = Struct->FindPropertyByName(FName(*Name)))
			{
				return Property;
			}
		}
		return nullptr;
	}

	bool EnumValue(const UEnum* Enum, const TSharedPtr<FJsonValue>& V, int64& Out)
	{
		FString Name;
		if (V->TryGetString(Name))
		{
			Out = Enum->GetValueByNameString(Name);
			return Out != INDEX_NONE;
		}
		double Number = 0.0;
		if (V->TryGetNumber(Number))
		{
			Out = int64(Number);
			return true;
		}
		return false;
	}

	bool SetFromJson(void* Container, FProperty* Property, const TSharedPtr<FJsonValue>& V)
	{
		void* Addr = Property->ContainerPtrToValuePtr<void>(Container);
		if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			bool bValue = false;
			double Number = 0.0;
			if (!V->TryGetBool(bValue))
			{
				if (!V->TryGetNumber(Number))
				{
					return false;
				}
				bValue = Number != 0.0;
			}
			Bool->SetPropertyValue(Addr, bValue);
			return true;
		}
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		{
			int64 Value = 0;
			if (!EnumValue(EnumProp->GetEnum(), V, Value))
			{
				return false;
			}
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(Addr, Value);
			return true;
		}
		if (FByteProperty* Byte = CastField<FByteProperty>(Property); Byte && Byte->Enum)
		{
			int64 Value = 0;
			if (!EnumValue(Byte->Enum, V, Value))
			{
				return false;
			}
			Byte->SetIntPropertyValue(Addr, Value);
			return true;
		}
		if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			double Number = 0.0;
			if (!V->TryGetNumber(Number))
			{
				return false;
			}
			if (Numeric->IsFloatingPoint())
			{
				Numeric->SetFloatingPointPropertyValue(Addr, Number);
			}
			else
			{
				Numeric->SetIntPropertyValue(Addr, int64(FMath::RoundToDouble(Number)));
			}
			return true;
		}
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!V->TryGetArray(Items))
			{
				return false;
			}
			auto At = [Items](int32 i, double Default) { return Items->IsValidIndex(i) ? (*Items)[i]->AsNumber() : Default; };
			const FName Type = StructProp->Struct->GetFName();
			if (Type == NAME_LinearColor)
			{
				*static_cast<FLinearColor*>(Addr) = FLinearColor(At(0, 0), At(1, 0), At(2, 0), At(3, 1));
				return true;
			}
			if (Type == NAME_Color)
			{
				*static_cast<FColor*>(Addr) = FColor(uint8(At(0, 0)), uint8(At(1, 0)), uint8(At(2, 0)), uint8(At(3, 255)));
				return true;
			}
			if (Type == NAME_Vector4)
			{
				*static_cast<FVector4*>(Addr) = FVector4(At(0, 0), At(1, 0), At(2, 0), At(3, 0));
				return true;
			}
			if (Type == NAME_Vector)
			{
				*static_cast<FVector*>(Addr) = FVector(At(0, 0), At(1, 0), At(2, 0));
				return true;
			}
		}
		return false;
	}
}

FRotator UMREnvironmentSubsystem::SkyBodyRotation(double Hour, double Rise, double Set, double MaxElevation, double& OutElevation)
{
	// Rises in the east-north-east, passes the south at its highest, sets in the west-north-west;
	// below the horizon it carries on round the north. Azimuth is the direction the light comes
	// from (UE yaw: 0 east, 90 south); the light points the other way.
	constexpr double AzimuthRise = -15.0;
	constexpr double AzimuthSet = 195.0;
	const double Span = FMath::Clamp(Set - Rise, 1.0, 23.0);
	const double Into = FMath::Fmod(FMath::Fmod(Hour - Rise, 24.0) + 24.0, 24.0);
	double Azimuth;
	if (Into <= Span)
	{
		const double F = Into / Span;
		OutElevation = MaxElevation * FMath::Sin(PI * F);
		Azimuth = AzimuthRise + F * (AzimuthSet - AzimuthRise);
	}
	else
	{
		const double G = (Into - Span) / (24.0 - Span);
		OutElevation = -0.6 * MaxElevation * FMath::Sin(PI * G);
		Azimuth = AzimuthSet + G * (AzimuthRise + 360.0 - AzimuthSet);
	}
	return FRotator(-OutElevation, Azimuth + 180.0, 0.0);
}

float UMREnvironmentSubsystem::LampsOnForHour(double Hour)
{
	// kod lamp.kod SetSwitchOnTime: off while 11 <= hour <= 17 (whole hours), on otherwise; a quarter
	// game hour (75 real seconds) of fade at each end
	const double H = FMath::Fmod(FMath::Fmod(Hour, 24.0) + 24.0, 24.0);
	const double Off = FMath::SmoothStep(10.75, 11.0, H) * (1.0 - FMath::SmoothStep(18.0, 18.25, H));
	return float(1.0 - Off);
}

bool UMREnvironmentSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// the dedicated server renders nothing
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UMREnvironmentSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
}

void UMREnvironmentSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);
	Collection = LoadObject<UMaterialParameterCollection>(nullptr, CollectionPath);
	FParse::Value(FCommandLine::Get(), TEXT("MRMood="), PinnedMood);
	Reload();
}

void UMREnvironmentSubsystem::Reload()
{
	Root.Reset();
	Resolved.Empty();
	LastApplied.Empty();
	bForce = true;
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("environment"), TEXT("moods.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogMeridian, Warning, TEXT("MREnvironment: no %s; lighting stays as the level has it"), *Path);
		return;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogMeridian, Error, TEXT("MREnvironment: could not parse %s"), *Path);
		Root.Reset();
	}
}

TSharedPtr<FJsonObject> UMREnvironmentSubsystem::ResolveMood(const FString& Name)
{
	if (const TSharedPtr<FJsonObject>* Found = Resolved.Find(Name))
	{
		return *Found;
	}
	const TSharedPtr<FJsonObject>* Moods = nullptr;
	const TSharedPtr<FJsonObject>* Mood = nullptr;
	if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("moods"), Moods) || !(*Moods)->TryGetObjectField(Name, Mood))
	{
		if (!Warned.Contains(Name))
		{
			Warned.Add(Name);
			UE_LOG(LogMeridian, Warning, TEXT("MREnvironment: moods.json has no mood %s"), *Name);
		}
		return nullptr;
	}
	// as zone_mood.py resolve_mood: start from the inherited mood; this mood's blocks and settings win
	TSharedPtr<FJsonObject> Merged;
	FString Base;
	if ((*Mood)->TryGetStringField(TEXT("inherit"), Base) && Base != Name)
	{
		const TSharedPtr<FJsonObject> Parent = ResolveMood(Base);
		Merged = Parent.IsValid() ? CopyObject(Parent) : MakeShared<FJsonObject>();
	}
	else
	{
		Merged = MakeShared<FJsonObject>();
	}
	for (const auto& Pair : (*Mood)->Values)
	{
		const FString MoodKey(*Pair.Key);
		if (MoodKey == TEXT("inherit") || MoodKey.StartsWith(TEXT("_")) || Pair.Value->Type != EJson::Object)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Existing = nullptr;
		TSharedPtr<FJsonObject> Target = Merged->TryGetObjectField(Pair.Key, Existing) ? *Existing : MakeShared<FJsonObject>();
		for (const auto& Field : Pair.Value->AsObject()->Values)
		{
			const TSharedPtr<FJsonObject>* Settings = nullptr;
			if (FString(*Field.Key) == TEXT("settings") && Target->TryGetObjectField(TEXT("settings"), Settings))
			{
				for (const auto& Setting : Field.Value->AsObject()->Values)
				{
					(*Settings)->SetField(Setting.Key, CopyValue(Setting.Value));
				}
			}
			else
			{
				Target->SetField(Field.Key, CopyValue(Field.Value));
			}
		}
		Merged->SetObjectField(Pair.Key, Target);
	}
	Resolved.Add(Name, Merged);
	return Merged;
}

TSharedPtr<FJsonObject> UMREnvironmentSubsystem::StateFor(double Hour, int32 ZoneId, bool& bOutPinnedMood)
{
	const FString CVar = CVarMood.GetValueOnGameThread();
	const FString Pinned = !CVar.IsEmpty() ? CVar : PinnedMood;
	bOutPinnedMood = !Pinned.IsEmpty();
	if (bOutPinnedMood)
	{
		const TSharedPtr<FJsonObject> Mood = ResolveMood(Pinned);
		return Mood.IsValid() ? CopyObject(Mood) : nullptr;
	}

	// zone profile -> cycle -> keys
	const TSharedPtr<FJsonObject>* Zones = nullptr;
	const TSharedPtr<FJsonObject>* Profile = nullptr;
	FString CycleName;
	if (Root->TryGetObjectField(TEXT("zones"), Zones)
		&& ((*Zones)->TryGetObjectField(FString::FromInt(ZoneId), Profile) || (*Zones)->TryGetObjectField(TEXT("default"), Profile)))
	{
		(*Profile)->TryGetStringField(TEXT("cycle"), CycleName);
	}
	const TSharedPtr<FJsonObject>* Cycles = nullptr;
	const TSharedPtr<FJsonObject>* Cycle = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
	if (!Root->TryGetObjectField(TEXT("cycles"), Cycles) || !(*Cycles)->TryGetObjectField(CycleName, Cycle)
		|| !(*Cycle)->TryGetArrayField(TEXT("keys"), KeyValues) || KeyValues->Num() == 0)
	{
		if (!Warned.Contains(TEXT("cycle:") + CycleName))
		{
			Warned.Add(TEXT("cycle:") + CycleName);
			UE_LOG(LogMeridian, Warning, TEXT("MREnvironment: no cycle '%s' for zone %d in moods.json"), *CycleName, ZoneId);
		}
		return nullptr;
	}
	TArray<TPair<double, FString>> Keys;
	for (const TSharedPtr<FJsonValue>& Key : *KeyValues)
	{
		const TArray<TSharedPtr<FJsonValue>>& Pair = Key->AsArray();
		if (Pair.Num() == 2)
		{
			Keys.Emplace(FMath::Fmod(Pair[0]->AsNumber(), 24.0), Pair[1]->AsString());
		}
	}
	Keys.Sort([](const TPair<double, FString>& A, const TPair<double, FString>& B) { return A.Key < B.Key; });

	// the keys around the hour, wrapping over midnight
	int32 Next = Keys.IndexOfByPredicate([Hour](const TPair<double, FString>& K) { return K.Key > Hour; });
	if (Next == INDEX_NONE)
	{
		Next = 0;
	}
	const int32 Prev = (Next + Keys.Num() - 1) % Keys.Num();
	double Span = Keys[Next].Key - Keys[Prev].Key;
	Span = Span <= 0.0 ? Span + 24.0 : Span;
	double Into = Hour - Keys[Prev].Key;
	Into = Into < 0.0 ? Into + 24.0 : Into;
	const double T = FMath::SmoothStep(0.0, 1.0, FMath::Clamp(Into / Span, 0.0, 1.0));

	const TSharedPtr<FJsonObject> A = ResolveMood(Keys[Prev].Value);
	const TSharedPtr<FJsonObject> B = ResolveMood(Keys[Next].Value);
	if (!A.IsValid() || !B.IsValid())
	{
		return A.IsValid() ? CopyObject(A) : (B.IsValid() ? CopyObject(B) : nullptr);
	}
	return BlendValue(MakeShared<FJsonValueObject>(A), MakeShared<FJsonValueObject>(B), T)->AsObject();
}

AActor* UMREnvironmentSubsystem::FindActor(const FString& Label) const
{
	const FName Tag(*Label);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->Tags.Contains(Tag))
		{
			return *It;
		}
#if WITH_EDITOR
		if (It->GetActorLabel() == Label)
		{
			return *It;
		}
#endif
	}
	return nullptr;
}

int32 UMREnvironmentSubsystem::LocalZoneId() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const AMRPlayerState* PS = PC ? PC->GetPlayerState<AMRPlayerState>() : nullptr;
	return PS ? PS->GetZoneId() : -1;
}

void UMREnvironmentSubsystem::ApplyLamps(float LampsOn)
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->Tags.Contains(NightLampTag))
		{
			continue;
		}
		TArray<ULightComponent*> Lights;
		It->GetComponents(Lights);
		for (ULightComponent* Light : Lights)
		{
			const float Base = LampBase.FindOrAdd(Light, Light->Intensity);
			Light->SetIntensity(Base * LampsOn);
			Light->SetVisibility(LampsOn > 0.001f);
		}
	}
}

void UMREnvironmentSubsystem::Apply(const TSharedPtr<FJsonObject>& State, double Hour, bool bPinnedMood)
{
	UWorld* World = GetWorld();
	const TSharedPtr<FJsonObject>* Sky = nullptr;
	Root->TryGetObjectField(TEXT("sky"), Sky);
	const TSharedPtr<FJsonObject> SkyCfg = Sky ? *Sky : nullptr;

	// the directional light: the sun by day, the moon by night (unless a pinned mood sets it)
	if (!bPinnedMood)
	{
		const TSharedPtr<FJsonObject>* SunBlock = nullptr;
		if (State->TryGetObjectField(TEXT("Sun"), SunBlock))
		{
			double SunElevation = 0.0, MoonElevation = 0.0;
			const FRotator SunRot = SkyBodyRotation(Hour, Number(SkyCfg, TEXT("sun_rise"), 6.0), Number(SkyCfg, TEXT("sun_set"), 22.0),
				Number(SkyCfg, TEXT("sun_max_elevation"), 50.0), SunElevation);
			const FRotator MoonRot = SkyBodyRotation(Hour, Number(SkyCfg, TEXT("moon_rise"), 18.0), Number(SkyCfg, TEXT("moon_set"), 34.0),
				Number(SkyCfg, TEXT("moon_max_elevation"), 40.0), MoonElevation);
			const bool bSun = SunElevation > 0.0;
			if (bSun)
			{
				(*SunBlock)->SetNumberField(TEXT("intensity"),
					Number(*SunBlock, TEXT("intensity"), 8.0) * FMath::SmoothStep(0.0, 5.0, SunElevation));
			}
			else
			{
				// moonlight as soon as the sun is down (the sun has faded to nothing by then, so only the
				// light's direction changes); the night mood's sky light keeps the shade from going black
				(*SunBlock)->SetNumberField(TEXT("intensity"),
					Number(SkyCfg, TEXT("moon_intensity"), 0.12) * FMath::SmoothStep(0.0, 5.0, MoonElevation));
				(*SunBlock)->SetNumberField(TEXT("temperature"), Number(SkyCfg, TEXT("moon_temperature"), 8000.0));
			}
			TArray<TSharedPtr<FJsonValue>> Rotation;
			const FRotator Rot = bSun ? SunRot : MoonRot;
			Rotation.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
			Rotation.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
			(*SunBlock)->SetArrayField(TEXT("rotation"), Rotation);
		}
	}

	// MPC_Environment: the moods' "Collection" plus the lamps
	const float LampsOn = bPinnedMood ? 1.0f : LampsOnForHour(Hour);
	const FString Key = ToJson(State) + FString::Printf(TEXT("|%.3f"), LampsOn);
	if (Key == LastApplied)
	{
		return;  // nothing changed (a pinned hour, or the same 5 seconds)
	}
	LastApplied = Key;

	if (Collection)
	{
		TMap<FString, double> Scalars = {{TEXT("WindowGlow"), 0.0}, {TEXT("Stars"), 0.0}};
		const TSharedPtr<FJsonObject>* Values = nullptr;
		if (State->TryGetObjectField(TEXT("Collection"), Values))
		{
			for (const auto& Pair : (*Values)->Values)
			{
				double Value = 0.0;
				const FString Name(*Pair.Key);
				if (!Name.StartsWith(TEXT("_")) && Pair.Value->TryGetNumber(Value))
				{
					Scalars.Add(Name, Value);
				}
			}
		}
		Scalars.Add(TEXT("LampsOn"), LampsOn);
		for (const auto& Pair : Scalars)
		{
			UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, FName(*Pair.Key), float(Pair.Value));
		}
	}
	ApplyLamps(LampsOn);

	for (const auto& Pair : State->Values)
	{
		const FString Label(*Pair.Key);
		if (Label.StartsWith(TEXT("_")) || Label == TEXT("Collection") || Pair.Value->Type != EJson::Object)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> Block = Pair.Value->AsObject();
		AActor* Actor = FindActor(Label);
		if (!Actor)
		{
			if (!Warned.Contains(TEXT("actor:") + Label))
			{
				Warned.Add(TEXT("actor:") + Label);
				UE_LOG(LogMeridian, Warning, TEXT("MREnvironment: the moods name %s, which is not in the world (tag or label)"), *Label);
			}
			continue;
		}
		const TSharedPtr<FJsonObject>* Settings = nullptr;
		if (APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor); Volume && Block->TryGetObjectField(TEXT("settings"), Settings))
		{
			for (const auto& Setting : (*Settings)->Values)
			{
				const FString SettingName(*Setting.Key);
				FProperty* Property = FindProperty(FPostProcessSettings::StaticStruct(), SettingName);
				if (!Property || !SetFromJson(&Volume->Settings, Property, Setting.Value))
				{
					if (!Warned.Contains(TEXT("pp:") + SettingName))
					{
						Warned.Add(TEXT("pp:") + SettingName);
						UE_LOG(LogMeridian, Warning, TEXT("MREnvironment: post process has no settable %s"), *SettingName);
					}
					continue;
				}
				if (FBoolProperty* Override = CastField<FBoolProperty>(
						FPostProcessSettings::StaticStruct()->FindPropertyByName(FName(*(TEXT("bOverride_") + Property->GetName())))))
				{
					Override->SetPropertyValue_InContainer(&Volume->Settings, true);
				}
			}
			continue;
		}
		USceneComponent* Component = Actor->GetRootComponent();
		if (!Component)
		{
			continue;
		}
		bool bChanged = false;
		for (const auto& Field : Block->Values)
		{
			const FString FieldName(*Field.Key);
			if (FieldName.StartsWith(TEXT("_")))
			{
				continue;
			}
			if (FieldName == TEXT("rotation"))
			{
				const TArray<TSharedPtr<FJsonValue>>& R = Field.Value->AsArray();
				if (R.Num() >= 2)
				{
					Actor->SetActorRotation(FRotator(R[0]->AsNumber(), R[1]->AsNumber(), 0.0));
				}
				continue;
			}
			FProperty* Property = FindProperty(Component->GetClass(), FieldName);
			if (Property && SetFromJson(Component, Property, Field.Value))
			{
				bChanged = true;
			}
			else if (!Warned.Contains(Label + TEXT(".") + FieldName))
			{
				Warned.Add(Label + TEXT(".") + FieldName);
				UE_LOG(LogMeridian, Warning, TEXT("MREnvironment: %s has no settable %s"), *Label, *FieldName);
			}
		}
		if (bChanged)
		{
			Component->MarkRenderStateDirty();
		}
	}
}

void UMREnvironmentSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !Root.IsValid() || (World->WorldType == EWorldType::Editor && CVarEditor.GetValueOnGameThread() == 0))
	{
		return;
	}
	const int32 Zone = LocalZoneId();
	const double Now = FPlatformTime::Seconds();
	if (Zone != LastZone)
	{
		LastZone = Zone;
		bForce = true;
	}
	if (!bForce && Now < NextUpdate)
	{
		return;
	}
	bForce = false;
	NextUpdate = Now + FMath::Max(0.1f, CVarUpdateSeconds.GetValueOnGameThread());

	const UMRGameTimeSubsystem* Time = World->GetSubsystem<UMRGameTimeSubsystem>();
	const double Hour = Time ? Time->GetGameHour() : 12.0;
	bool bPinnedMood = false;
	const TSharedPtr<FJsonObject> State = StateFor(Hour, Zone, bPinnedMood);
	if (State.IsValid())
	{
		Apply(State, Hour, bPinnedMood);
	}
}

TStatId UMREnvironmentSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMREnvironmentSubsystem, STATGROUP_Tickables);
}

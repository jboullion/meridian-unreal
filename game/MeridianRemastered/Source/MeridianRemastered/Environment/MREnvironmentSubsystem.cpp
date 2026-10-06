#include "Environment/MREnvironmentSubsystem.h"

#include "Audio/MRAudioSubsystem.h"

#include "Components/LightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/MRGameTimeSubsystem.h"
#include "Environment/MRPrecipitationActor.h"
#include "Game/MRGameState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/VolumetricCloudComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
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
	TAutoConsoleVariable<int32> CVarParticles(
		TEXT("mr.Weather.Particles"), 1,
		TEXT("0: no falling rain or snow (a player setting, as the original's); wet and snowy ground stay."));
	TAutoConsoleVariable<FString> CVarKind(
		TEXT("mr.Weather.Kind"), TEXT(""),
		TEXT("rain, snow or sand: what storms bring here, whatever the season and the zone's mask (look-dev); empty follows them."));
	TAutoConsoleVariable<int32> CVarSplashes(
		TEXT("mr.Weather.Splashes"), 1, TEXT("0: no rain splashes on the ground."));
	TAutoConsoleVariable<int32> CVarSounds(
		TEXT("mr.Weather.Sounds"), 1, TEXT("0: no rain, wind or thunder sounds."));
	TAutoConsoleVariable<int32> CVarLightning(
		TEXT("mr.Weather.Lightning"), 1, TEXT("0: no lightning in rainstorms."));
	TAutoConsoleVariable<int32> CVarAmbient(
		TEXT("mr.Env.Ambient"), 1, TEXT("0: no ambient particles (dust motes, pollen, fireflies, leaves); a player setting."));
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

	/** An overlay mood on top of a state: numbers and arrays replace, {"mul": x} multiplies and
	    {"add": x} adds (a number, or per component for arrays), objects ("settings") recurse.
	    Returns a new object. */
	TSharedPtr<FJsonObject> Overlay(const TSharedPtr<FJsonObject>& Base, const TSharedPtr<FJsonObject>& Over)
	{
		TSharedPtr<FJsonObject> Out = CopyObject(Base);
		for (const auto& Pair : Over->Values)
		{
			const FString Key(*Pair.Key);
			if (Key.StartsWith(TEXT("_")) || Key == TEXT("inherit"))
			{
				continue;
			}
			const TSharedPtr<FJsonValue> Old = Out->TryGetField(Pair.Key);
			const TSharedPtr<FJsonObject>* OverObj = nullptr;
			if (Pair.Value->TryGetObject(OverObj))
			{
				const TSharedPtr<FJsonValue> Mul = (*OverObj)->TryGetField(TEXT("mul"));
				const TSharedPtr<FJsonValue> Add = (*OverObj)->TryGetField(TEXT("add"));
				if (Mul.IsValid() || Add.IsValid())
				{
					const bool bMul = Mul.IsValid();
					const TSharedPtr<FJsonValue> Op = bMul ? Mul : Add;
					const double Identity = bMul ? 1.0 : 0.0;
					auto Combine = [bMul](double A, double B) { return bMul ? A * B : A + B; };
					if (Old.IsValid() && Old->Type == EJson::Number && Op->Type == EJson::Number)
					{
						Out->SetNumberField(Pair.Key, Combine(Old->AsNumber(), Op->AsNumber()));
					}
					else if (Old.IsValid() && Old->Type == EJson::Array)
					{
						TArray<TSharedPtr<FJsonValue>> Items;
						const TArray<TSharedPtr<FJsonValue>>& A = Old->AsArray();
						for (int32 i = 0; i < A.Num(); ++i)
						{
							const double M = Op->Type == EJson::Array
								? (Op->AsArray().IsValidIndex(i) ? Op->AsArray()[i]->AsNumber() : Identity) : Op->AsNumber();
							Items.Add(MakeShared<FJsonValueNumber>(Combine(A[i]->AsNumber(), M)));
						}
						Out->SetArrayField(Pair.Key, Items);
					}
					continue;
				}
				const TSharedPtr<FJsonObject>* OldObj = nullptr;
				Out->SetObjectField(Pair.Key, Overlay(Old.IsValid() && Old->TryGetObject(OldObj) ? *OldObj : MakeShared<FJsonObject>(), *OverObj));
				continue;
			}
			Out->SetField(Pair.Key, CopyValue(Pair.Value));
		}
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

double UMREnvironmentSubsystem::RoomLightForHour(double Hour, double BaseLight, double OutsideFactor)
{
	const double H = FMath::Fmod(FMath::Fmod(Hour, 24.0) + 24.0, 24.0);
	const int32 H0 = FMath::FloorToInt32(H);
	const double Brightness = FMath::Lerp(double(UMRGameTimeSubsystem::BrightnessForHour(H0)),
		double(UMRGameTimeSubsystem::BrightnessForHour((H0 + 1) % 24)), H - H0);
	return FMath::Clamp(BaseLight + OutsideFactor * (Brightness - 50.0) / 4.0, 0.0, 255.0) / 255.0;
}

FMRAtmosphere UMREnvironmentSubsystem::AtmosphereFor(double Hour, double SunElevation, int32 Season, float Storm, bool bOutdoor,
	const TSharedPtr<FJsonObject>& Cfg, const TSharedPtr<FJsonObject>& ZoneAmbient)
{
	FMRAtmosphere A;
	const double H = FMath::Fmod(FMath::Fmod(Hour, 24.0) + 24.0, 24.0);
	const int32 S = FMath::Clamp(Season, 0, 3);
	auto Numbers = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<double> Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (Obj.IsValid() && Obj->TryGetArrayField(Field, Items) && Items->Num() == Out.Num())
		{
			for (int32 i = 0; i < Out.Num(); ++i)
			{
				Out[i] = (*Items)[i]->AsNumber();
			}
		}
		return Out;
	};
	auto Child = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Obj.IsValid() && Obj->TryGetObjectField(Field, Out) ? *Out : TSharedPtr<FJsonObject>();
	};

	// dark from night_deg [below, above] the horizon: 1 below the first, 0 above the second;
	// quantised, as it changes at every update
	const TArray<double> Deg = Numbers(Cfg, TEXT("night_deg"), {-4.0, 6.0});
	A.Night = float(FMath::RoundToDouble((1.0 - FMath::SmoothStep(Deg[0], Deg[1], SunElevation)) * 50.0) / 50.0);

	// the season's tint (summer is the original's colours), and snow lying outdoors in winter
	const TSharedPtr<FJsonObject> Seasons = Child(Cfg, TEXT("seasons"));
	const float Tint = float(Numbers(Seasons, TEXT("tint"), {1.0, 0.0, 1.0, 1.0})[S]);
	A.Spring = S == 0 ? Tint : 0.f;
	A.Autumn = S == 2 ? Tint : 0.f;
	A.Winter = S == 3 ? Tint : 0.f;
	A.WinterCover = bOutdoor && S == 3 ? float(Number(Seasons, TEXT("winter_snow_cover"), 0.0)) : 0.f;

	// chimney smoke: a base, more on cold mornings (a bump over morning_hours: rising from, full,
	// full until, gone by), at night and in winter, less in a storm (the wind and the rain take it)
	const TSharedPtr<FJsonObject> Smoke = Child(Cfg, TEXT("smoke"));
	const TArray<double> M = Numbers(Smoke, TEXT("morning_hours"), {4.0, 6.0, 9.0, 11.0});
	const double Bump = FMath::SmoothStep(M[0], M[1], H) * (1.0 - FMath::SmoothStep(M[2], M[3], H));
	const double SmokeAmount = Number(Smoke, TEXT("base"), 0.45) + Number(Smoke, TEXT("morning"), 0.35) * Bump
		+ Number(Smoke, TEXT("night"), 0.1) * A.Night + (S == 3 ? Number(Smoke, TEXT("winter"), 0.35) : 0.0);
	A.Smoke = float(FMath::Clamp(SmokeAmount * (1.0 - Number(Smoke, TEXT("storm"), 0.6) * Storm), 0.0, 1.0));

	// ambient particles: the zone's weight for each kind x the kind's season x its time of day
	// ("when": day, night or always) x the storm outdoors ("storm": the share a full storm takes away;
	// negative brings more)
	const TSharedPtr<FJsonObject> Kinds = Child(Cfg, TEXT("ambient"));
	auto Kind = [&](const TCHAR* Name)
	{
		const double Weight = Number(ZoneAmbient, Name, 0.0);
		const TSharedPtr<FJsonObject> K = Child(Kinds, Name);
		if (Weight <= 0.0 || !K.IsValid())
		{
			return 0.f;
		}
		FString When;
		K->TryGetStringField(TEXT("when"), When);
		const double Time = When == TEXT("day") ? 1.0 - A.Night : (When == TEXT("night") ? A.Night : 1.0);
		const double Weather = bOutdoor ? 1.0 - Number(K, TEXT("storm"), 0.0) * Storm : 1.0;
		return float(FMath::Clamp(Weight * Numbers(K, TEXT("seasons"), {1.0, 1.0, 1.0, 1.0})[S] * Time * Weather, 0.0, 2.0));
	};
	A.Motes = Kind(TEXT("motes"));
	A.Pollen = Kind(TEXT("pollen"));
	A.Fireflies = Kind(TEXT("fireflies"));
	A.Leaves = Kind(TEXT("leaves"));
	return A;
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
	bNoLightning = FParse::Param(FCommandLine::Get(), TEXT("MRNoLightning"));  // look-dev stills
	FParse::Value(FCommandLine::Get(), TEXT("MRWeatherKind="), KindOverride);
	bHoldLightning = FParse::Param(FCommandLine::Get(), TEXT("MRLightningHold"));
	bNoLightning = bNoLightning && !bHoldLightning;
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
	// the zone's profile ("zones": cycle, kind, sun, daylight, lamps)
	const TSharedPtr<FJsonObject>* Zones = nullptr;
	const TSharedPtr<FJsonObject>* ZoneProfile = nullptr;
	Profile.Reset();
	if (Root->TryGetObjectField(TEXT("zones"), Zones)
		&& ((*Zones)->TryGetObjectField(FString::FromInt(ZoneId), ZoneProfile) || (*Zones)->TryGetObjectField(TEXT("default"), ZoneProfile)))
	{
		Profile = *ZoneProfile;
	}

	const FString CVar = CVarMood.GetValueOnGameThread();
	const FString Pinned = !CVar.IsEmpty() ? CVar : PinnedMood;
	bOutPinnedMood = !Pinned.IsEmpty();
	if (bOutPinnedMood)
	{
		const TSharedPtr<FJsonObject> Mood = ResolveMood(Pinned);
		return Mood.IsValid() ? CopyObject(Mood) : nullptr;
	}

	// zone profile -> cycle -> keys
	FString CycleName;
	if (Profile.IsValid())
	{
		Profile->TryGetStringField(TEXT("cycle"), CycleName);
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
	// the zone the view is in: the same as the player's in play, but look-dev (and spectating) move
	// the camera without the player
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager)
	{
		if (const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>())
		{
			if (const int32 Zone = Zones->ZoneAtLocation(PC->PlayerCameraManager->GetCameraLocation()))
			{
				return Zone;
			}
		}
	}
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

	// interiors and underground: no sun or moon (their single-sided walls and ceilings don't stop the
	// sun's shadows); the rotation stays as it was, so the shadow cache isn't touched
	bool bSunOn = true;
	if (Profile.IsValid() && Profile->TryGetBoolField(TEXT("sun"), bSunOn) && !bSunOn)
	{
		const TSharedPtr<FJsonObject>* SunBlock = nullptr;
		if (State->TryGetObjectField(TEXT("Sun"), SunBlock))
		{
			(*SunBlock)->SetNumberField(TEXT("intensity"), 0.0);
			(*SunBlock)->RemoveField(TEXT("rotation"));
		}
	}
	const double Daylight = Number(Profile, TEXT("daylight"), 1.0);
	FString LampRule;
	const bool bLampsAlways = Profile.IsValid() && Profile->TryGetStringField(TEXT("lamps"), LampRule) && LampRule == TEXT("always");

	// MPC_Environment: the moods' "Collection" plus the lamps
	const float LampsOn = bPinnedMood || bLampsAlways ? 1.0f : LampsOnForHour(Hour);
	// the room's own light by the hour, as the original computes it (interiors and underground)
	const double RoomLight = RoomLightForHour(Hour, Number(Profile, TEXT("base_light"), 255.0), Number(Profile, TEXT("outside"), 10.0));
	const bool bOutdoor = IsOutdoor();
	const float PrecipOn = bOutdoor && WeatherKind != EMRWeatherKind::None ? StormAmount : 0.f;
	const FMRAtmosphere& Atmo = Atmosphere;
	const FString Key = ToJson(State) + FString::Printf(TEXT("|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%d"), LampsOn, Daylight, RoomLight,
		Wetness, SnowCover, WindAmount, PrecipOn, int32(WeatherKind))
		+ FString::Printf(TEXT("|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f"), Atmo.Night, Atmo.Smoke, Atmo.Motes, Atmo.Pollen,
			Atmo.Fireflies, Atmo.Leaves, Atmo.Spring, Atmo.Autumn, Atmo.Winter);
	if (Key == LastApplied)
	{
		return;  // nothing changed (a pinned hour, or the same 5 seconds)
	}
	LastApplied = Key;
	UE_LOG(LogMeridian, Verbose, TEXT("MREnvironment: applied zone %d at hour %.2f (frame %llu)"), LastZone, Hour, (unsigned long long)GFrameCounter);

	if (Collection)
	{
		TMap<FString, double> Scalars = {{TEXT("WindowGlow"), 0.0}, {TEXT("Stars"), 0.0}, {TEXT("WindowDaylight"), 0.0},
			{TEXT("SectorAmbient"), 0.0}};
		TMap<FString, FLinearColor> Vectors = {{TEXT("AmbientTint"), FLinearColor::White}};
		const TSharedPtr<FJsonObject>* Values = nullptr;
		if (State->TryGetObjectField(TEXT("Collection"), Values))
		{
			for (const auto& Pair : (*Values)->Values)
			{
				double Value = 0.0;
				const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
				const FString Name(*Pair.Key);
				if (Name.StartsWith(TEXT("_")))
				{
					continue;
				}
				if (Pair.Value->TryGetNumber(Value))
				{
					Scalars.Add(Name, Value);
				}
				else if (Pair.Value->TryGetArray(Items) && Items->Num() >= 3)
				{
					Vectors.Add(Name, FLinearColor((*Items)[0]->AsNumber(), (*Items)[1]->AsNumber(), (*Items)[2]->AsNumber(),
						Items->Num() > 3 ? (*Items)[3]->AsNumber() : 1.0));
				}
			}
		}
		if (double* Ambient = Scalars.Find(TEXT("SectorAmbient")))
		{
			*Ambient *= RoomLight;  // dims with the hour where the room has windows (outside factor)
		}
		for (const auto& Pair : Vectors)
		{
			UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, FName(*Pair.Key), Pair.Value);
		}
		Scalars.Add(TEXT("LampsOn"), LampsOn);
		if (double* WindowDaylight = Scalars.Find(TEXT("WindowDaylight")))
		{
			*WindowDaylight *= Daylight;  // the zone's share of daylight through its windows
			BaseWindowDaylight = float(*WindowDaylight);
		}
		// weather: wet or white ground outdoors, wind, and what falls (M_Precip)
		Scalars.Add(TEXT("Wetness"), bOutdoor ? Wetness : 0.0);
		Scalars.Add(TEXT("SnowCover"), bOutdoor ? SnowCover : 0.0);
		Scalars.Add(TEXT("Wind"), WindAmount);
		Scalars.Add(TEXT("Precip"), PrecipOn);
		Scalars.Add(TEXT("Snow"), WeatherKind == EMRWeatherKind::Snow ? 1.0 : 0.0);
		Scalars.Add(TEXT("Sand"), WeatherKind == EMRWeatherKind::Sand ? 1.0 : 0.0);
		// atmosphere (phase 5)
		Scalars.Add(TEXT("Night"), Atmo.Night);
		Scalars.Add(TEXT("Smoke"), Atmo.Smoke);
		Scalars.Add(TEXT("Motes"), Atmo.Motes);
		Scalars.Add(TEXT("Pollen"), Atmo.Pollen);
		Scalars.Add(TEXT("Fireflies"), Atmo.Fireflies);
		Scalars.Add(TEXT("Leaves"), Atmo.Leaves);
		Scalars.Add(TEXT("Spring"), Atmo.Spring);
		Scalars.Add(TEXT("Autumn"), Atmo.Autumn);
		Scalars.Add(TEXT("Winter"), Atmo.Winter);
		for (const auto& Pair : Scalars)
		{
			UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, FName(*Pair.Key), float(Pair.Value));
		}
	}
	ApplyLamps(LampsOn);

	// "CloudMaterial": parameters of the clouds' material; any a mood set before and this state
	// doesn't name go back to the material's own values
	{
		const TSharedPtr<FJsonObject>* CloudParams = nullptr;
		const bool bHas = State->TryGetObjectField(TEXT("CloudMaterial"), CloudParams);
		if (bHas || CloudBaseValues.Num() > 0)
		{
			if (UMaterialInstanceDynamic* MID = CloudMaterial())
			{
				TMap<FName, float> Values;
				if (bHas)
				{
					for (const auto& Param : (*CloudParams)->Values)
					{
						double Value = 0.0;
						if (!FString(*Param.Key).StartsWith(TEXT("_")) && Param.Value->TryGetNumber(Value))
						{
							const FName Name(*FString(*Param.Key));
							CloudBase(Name);
							Values.Add(Name, float(Value));
						}
					}
				}
				for (const auto& Base : CloudBaseValues)
				{
					const float* Value = Values.Find(Base.Key);
					MID->SetScalarParameterValue(Base.Key, Value ? *Value : Base.Value);
				}
			}
		}
	}

	for (const auto& Pair : State->Values)
	{
		const FString Label(*Pair.Key);
		if (Label.StartsWith(TEXT("_")) || Label == TEXT("Collection") || Label == TEXT("CloudMaterial") || Pair.Value->Type != EJson::Object)
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
		// a zone change is a jump of up to 2 km (the buildings are their own levels): cut the
		// camera, so Lumen's and the exposure's history start from the new place instead of
		// blending the old one's lighting in and out over the next moment
		if (LastZone > 0 && World->IsGameWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->SetGameCameraCutThisFrame();
			}
			// the sky light's real-time capture is time-sliced: after the sun turns off (in) or on
			// (out), it kept lighting the new place with the old sky for about 2 s, the fade players
			// saw at every door (build/lookdev/burst_sky.png). Capture it whole every frame for a
			// moment, then go back to slicing.
			if (IConsoleVariable* Slice = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkyLight.RealTimeReflectionCapture.TimeSlice")))
			{
				if (SkySliceRestoreAt <= 0.0)
				{
					SkySliceSaved = Slice->GetInt();
				}
				Slice->Set(0, ECVF_SetByCode);
				SkySliceRestoreAt = Now + 2.0;
			}
		}
		LastZone = Zone;
		bForce = true;
	}
	if (SkySliceRestoreAt > 0.0 && Now >= SkySliceRestoreAt)
	{
		if (IConsoleVariable* Slice = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkyLight.RealTimeReflectionCapture.TimeSlice")))
		{
			Slice->Set(SkySliceSaved, ECVF_SetByCode);
		}
		SkySliceRestoreAt = 0.0;
	}
	TickLightning(DeltaTime);
	if (!bForce && Now < NextUpdate)
	{
		return;
	}
	bForce = false;
	const UMRGameTimeSubsystem* Time = World->GetSubsystem<UMRGameTimeSubsystem>();
	const double Hour = Time ? Time->GetGameHour() : 12.0;
	bool bPinnedMood = false;
	TSharedPtr<FJsonObject> State = StateFor(Hour, Zone, bPinnedMood);  // also picks the zone's profile
	UpdateWeather();
	// while a storm builds or clears, follow it every second
	const bool bChanging = (StormAmount > 0.001f && StormAmount < 0.999f) || (Wetness > 0.001f && Wetness < 0.999f)
		|| (SnowCover > 0.001f && SnowCover < 0.999f);
	NextUpdate = Now + (bChanging ? 1.0 : FMath::Max(0.1f, CVarUpdateSeconds.GetValueOnGameThread()));
	UpdateAtmosphere(Hour);  // after: winter's lying snow isn't a storm changing
	if (State.IsValid())
	{
		State = WithStorm(State);
		Apply(State, Hour, bPinnedMood);
	}
	UpdatePrecipitation(Zone);
	UpdateAmbient(Zone);
	UpdateSounds();
}

TSharedPtr<FJsonValue> UMREnvironmentSubsystem::ProfileField(const TCHAR* Field) const
{
	if (Profile.IsValid() && Profile->HasField(Field))
	{
		return Profile->TryGetField(Field);
	}
	const TSharedPtr<FJsonObject>* Zones = nullptr;
	const TSharedPtr<FJsonObject>* Default = nullptr;
	if (Root.IsValid() && Root->TryGetObjectField(TEXT("zones"), Zones) && (*Zones)->TryGetObjectField(TEXT("default"), Default))
	{
		return (*Default)->TryGetField(Field);
	}
	return nullptr;
}

bool UMREnvironmentSubsystem::IsOutdoor() const
{
	FString Kind;
	return !(Profile.IsValid() && Profile->TryGetStringField(TEXT("kind"), Kind) && Kind != TEXT("outdoor"));
}

void UMREnvironmentSubsystem::UpdateWeather()
{
	const TSharedPtr<FJsonValue> ZoneValue = ProfileField(TEXT("weather_zone"));
	const TSharedPtr<FJsonValue> MaskValue = ProfileField(TEXT("weather_mask"));
	const int32 WeatherZone = ZoneValue.IsValid() ? int32(ZoneValue->AsNumber()) : 0;
	const int32 Mask = MaskValue.IsValid() ? MRWeather::MaskByName(MaskValue->AsString()) : -1;
	WeatherMask = Mask;
	const UMRGameTimeSubsystem* Time = GetWorld()->GetSubsystem<UMRGameTimeSubsystem>();
	const AMRGameState* GameState = GetWorld()->GetGameState<AMRGameState>();
	const FMRZoneWeather W = GameState && WeatherZone > 0 ? GameState->GetZoneWeather(WeatherZone) : FMRZoneWeather();
	WeatherKind = MRWeather::KindFor(Mask, Time ? Time->GetSeason() : 0);
	const FString Kind = !CVarKind.GetValueOnGameThread().IsEmpty() ? CVarKind.GetValueOnGameThread() : KindOverride;
	if (WeatherKind != EMRWeatherKind::None && !Kind.IsEmpty())
	{
		WeatherKind = Kind == TEXT("snow") ? EMRWeatherKind::Snow : (Kind == TEXT("sand") ? EMRWeatherKind::Sand : EMRWeatherKind::Rain);
	}
	bStormy = W.bStorm && WeatherKind != EMRWeatherKind::None;
	const double Since = W.SinceUnix > 0
		? double((FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds()) - double(W.SinceUnix) : 1.0e9;
	const TSharedPtr<FJsonObject>* WeatherCfg = nullptr;
	const TSharedPtr<FJsonObject> Cfg = Root->TryGetObjectField(TEXT("weather"), WeatherCfg) ? *WeatherCfg : nullptr;
	StormAmount = WeatherKind == EMRWeatherKind::None ? 0.f
		: MRWeather::StormAmount(bStormy, Since, Number(Cfg, TEXT("build_seconds"), 90.0), Number(Cfg, TEXT("clear_seconds"), 120.0));
	Wetness = WeatherKind == EMRWeatherKind::Rain
		? MRWeather::Cover(bStormy, Since, Number(Cfg, TEXT("wet_seconds"), 180.0), Number(Cfg, TEXT("dry_seconds"), 600.0)) : 0.f;
	SnowCover = WeatherKind == EMRWeatherKind::Snow
		? MRWeather::Cover(bStormy, Since, Number(Cfg, TEXT("snow_seconds"), 300.0), Number(Cfg, TEXT("melt_seconds"), 900.0)) : 0.f;
	WindAmount = float(FMath::Lerp(Number(Cfg, TEXT("wind_clear"), 1.0), Number(Cfg, TEXT("wind_storm"), 2.5), double(StormAmount)));
}

UMaterialInstanceDynamic* UMREnvironmentSubsystem::CloudMaterial()
{
	if (CloudMID.IsValid())
	{
		return CloudMID.Get();
	}
	AActor* Clouds = FindActor(TEXT("Clouds"));
	UVolumetricCloudComponent* Cloud = Clouds ? Clouds->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
	UMaterialInterface* Current = Cloud ? Cloud->Material.LoadSynchronous() : nullptr;
	if (!Current)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Current);
	if (!MID)
	{
		MID = UMaterialInstanceDynamic::Create(Current, Cloud);
		Cloud->SetMaterial(MID);
	}
	CloudMID = MID;
	return MID;
}

float UMREnvironmentSubsystem::CloudBase(const FName& Param)
{
	if (const float* Found = CloudBaseValues.Find(Param))
	{
		return *Found;
	}
	float Value = 0.f;
	if (UMaterialInstanceDynamic* MID = CloudMaterial())
	{
		MID->GetScalarParameterValue(FMaterialParameterInfo(Param), Value);
	}
	CloudBaseValues.Add(Param, Value);
	return Value;
}

TSharedPtr<FJsonObject> UMREnvironmentSubsystem::WithStorm(const TSharedPtr<FJsonObject>& State)
{
	if (StormAmount <= 0.001f || WeatherKind == EMRWeatherKind::None)
	{
		return State;
	}
	const TSharedPtr<FJsonObject>* WeatherCfg = nullptr;
	const TSharedPtr<FJsonObject>* Storms = nullptr;
	const TSharedPtr<FJsonObject>* Moods = nullptr;
	const TSharedPtr<FJsonObject>* Over = nullptr;
	FString Name;
	const TCHAR* KindName = WeatherKind == EMRWeatherKind::Snow ? TEXT("snow") : (WeatherKind == EMRWeatherKind::Sand ? TEXT("sand") : TEXT("rain"));
	// indoors the storm only darkens what comes through the windows
	const TCHAR* Which = IsOutdoor() ? KindName : TEXT("interior");
	if (!Root->TryGetObjectField(TEXT("weather"), WeatherCfg) || !(*WeatherCfg)->TryGetObjectField(TEXT("storm"), Storms)
		|| !(*Storms)->TryGetStringField(Which, Name) || !Root->TryGetObjectField(TEXT("moods"), Moods)
		|| !(*Moods)->TryGetObjectField(Name, Over))
	{
		return State;
	}
	// the clouds' own values under the storm's, so they blend rather than jump
	const TSharedPtr<FJsonObject>* OverClouds = nullptr;
	TSharedPtr<FJsonObject> Base = State;
	if ((*Over)->TryGetObjectField(TEXT("CloudMaterial"), OverClouds))
	{
		Base = CopyObject(State);
		const TSharedPtr<FJsonObject>* Have = nullptr;
		TSharedPtr<FJsonObject> Clouds = Base->TryGetObjectField(TEXT("CloudMaterial"), Have) ? *Have : MakeShared<FJsonObject>();
		for (const auto& Param : (*OverClouds)->Values)
		{
			if (!FString(*Param.Key).StartsWith(TEXT("_")) && !Clouds->HasField(Param.Key))
			{
				Clouds->SetNumberField(Param.Key, CloudBase(FName(*FString(*Param.Key))));
			}
		}
		Base->SetObjectField(TEXT("CloudMaterial"), Clouds);
	}
	// quantised, so a building storm re-applies in steps rather than every second for small changes
	const double S = FMath::RoundToDouble(StormAmount * 50.0) / 50.0;
	return BlendValue(MakeShared<FJsonValueObject>(Base), MakeShared<FJsonValueObject>(Overlay(Base, *Over)), S)->AsObject();
}

AMRPrecipitationActor* UMREnvironmentSubsystem::EnsurePrecip()
{
	if (!Precip.IsValid())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Precip = GetWorld()->SpawnActor<AMRPrecipitationActor>(Params);
		PrecipZone = -1;
		AmbientZone = -1;
	}
	return Precip.Get();
}

UMaterialInterface* UMREnvironmentSubsystem::ZoneMaterial(const TCHAR* Prefix, int32 Zone)
{
	// the zone's instance carries its shelter map (build_world.py: <Prefix>_<rid>), else the default
	const FString Key = FString::Printf(TEXT("%s_%d"), Prefix, Zone);
	TWeakObjectPtr<UMaterialInterface>& Cached = ZoneMaterials.FindOrAdd(Key);
	if (!Cached.IsValid())
	{
		const FString Base = TEXT("/Game/Generated/Environment/Materials/");
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *(Base + Key + TEXT(".") + Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
		Cached = M ? M : LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s%s.%s"), *Base, Prefix, Prefix), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	return Cached.Get();
}

void UMREnvironmentSubsystem::UpdatePrecipitation(int32 Zone)
{
	const bool bFalls = IsOutdoor() && StormAmount > 0.001f && CVarParticles.GetValueOnGameThread() != 0
		&& WeatherKind != EMRWeatherKind::None;
	if (!bFalls && !Precip.IsValid())
	{
		return;
	}
	AMRPrecipitationActor* Actor = EnsurePrecip();
	if (!Actor)
	{
		return;
	}
	if (bFalls && Zone != PrecipZone)
	{
		PrecipZone = Zone;
		Actor->SetMaterials(ZoneMaterial(TEXT("MI_Precip"), Zone), ZoneMaterial(TEXT("MI_Splash"), Zone));
	}
	Actor->SetFalling(bFalls, bFalls && WeatherKind == EMRWeatherKind::Rain && CVarSplashes.GetValueOnGameThread() != 0);
}

void UMREnvironmentSubsystem::UpdateAtmosphere(double Hour)
{
	const TSharedPtr<FJsonObject>* Sky = nullptr;
	const TSharedPtr<FJsonObject> SkyCfg = Root.IsValid() && Root->TryGetObjectField(TEXT("sky"), Sky) ? *Sky : nullptr;
	double Elevation = 0.0;
	SkyBodyRotation(Hour, Number(SkyCfg, TEXT("sun_rise"), 6.0), Number(SkyCfg, TEXT("sun_set"), 22.0),
		Number(SkyCfg, TEXT("sun_max_elevation"), 50.0), Elevation);
	const UMRGameTimeSubsystem* Time = GetWorld()->GetSubsystem<UMRGameTimeSubsystem>();
	const TSharedPtr<FJsonObject>* AtmoCfg = nullptr;
	const TSharedPtr<FJsonValue> Ambient = ProfileField(TEXT("ambient"));
	Atmosphere = AtmosphereFor(Hour, Elevation, Time ? Time->GetSeason() : 1, StormAmount, IsOutdoor(),
		Root.IsValid() && Root->TryGetObjectField(TEXT("atmosphere"), AtmoCfg) ? *AtmoCfg : nullptr,
		Ambient.IsValid() && Ambient->Type == EJson::Object ? Ambient->AsObject() : nullptr);
	const FString Line = FString::Printf(TEXT("night %.2f, smoke %.2f, motes %.2f, pollen %.2f, fireflies %.2f, leaves %.2f, season %d"),
		Atmosphere.Night, Atmosphere.Smoke, Atmosphere.Motes, Atmosphere.Pollen, Atmosphere.Fireflies, Atmosphere.Leaves,
		Time ? Time->GetSeason() : -1);
	if (Line != LastAtmosphereLog)
	{
		LastAtmosphereLog = Line;
		UE_LOG(LogMeridian, Log, TEXT("MREnvironment: atmosphere %s"), *Line);
	}
	// winter: a light snow lies outdoors where winter storms bring snow, storm or not
	if (WeatherKind == EMRWeatherKind::Snow)
	{
		SnowCover = FMath::Max(SnowCover, Atmosphere.WinterCover);
	}
}

void UMREnvironmentSubsystem::UpdateAmbient(int32 Zone)
{
	const FMRAtmosphere& A = Atmosphere;
	const bool bShow = CVarAmbient.GetValueOnGameThread() != 0 && (A.Motes + A.Pollen + A.Fireflies + A.Leaves) > 0.001f;
	if (!bShow && !Precip.IsValid())
	{
		return;
	}
	AMRPrecipitationActor* Actor = EnsurePrecip();
	if (!Actor)
	{
		return;
	}
	UMaterialInterface* Material = nullptr;
	if (bShow && Zone != AmbientZone)
	{
		AmbientZone = Zone;
		Material = ZoneMaterial(TEXT("MI_Ambient"), Zone);
	}
	Actor->SetAmbient(Material, bShow);
}

FString UMREnvironmentSubsystem::WeatherSoundFile(const TCHAR* Key) const
{
	// moods.json "weather" "sounds": {key: the original's file name without .ogg}
	const TSharedPtr<FJsonObject>* WeatherCfg = nullptr;
	const TSharedPtr<FJsonObject>* SoundsCfg = nullptr;
	FString Name;
	if (Root.IsValid() && Root->TryGetObjectField(TEXT("weather"), WeatherCfg) && (*WeatherCfg)->TryGetObjectField(TEXT("sounds"), SoundsCfg)
		&& (*SoundsCfg)->TryGetStringField(Key, Name))
	{
		return Name + TEXT(".ogg");
	}
	return FString();
}

void UMREnvironmentSubsystem::UpdateSounds()
{
	// moods.json weather.sounds.follow_mask: only where the original played them (the mask's sound bit)
	const TSharedPtr<FJsonObject>* WeatherCfg = nullptr;
	const TSharedPtr<FJsonObject>* SoundsCfg = nullptr;
	bool bFollowMask = false;
	if (Root.IsValid() && Root->TryGetObjectField(TEXT("weather"), WeatherCfg) && (*WeatherCfg)->TryGetObjectField(TEXT("sounds"), SoundsCfg))
	{
		(*SoundsCfg)->TryGetBoolField(TEXT("follow_mask"), bFollowMask);
	}
	const bool bOn = CVarSounds.GetValueOnGameThread() != 0 && !(bFollowMask && (WeatherMask < 0 || !(WeatherMask & 0x1)));
	bSoundsOn = bOn;
	const float Inside = IsOutdoor() ? 1.f : 0.5f;
	// played by the audio system (docs/adr/0006), muffled inside: heard through the walls
	if (UMRAudioSubsystem* Audio = GetWorld()->GetSubsystem<UMRAudioSubsystem>())
	{
		Audio->SetLoop2D(TEXT("weather.rain"), WeatherSoundFile(TEXT("rain")),
			bOn && WeatherKind == EMRWeatherKind::Rain ? StormAmount * Inside : 0.f, !IsOutdoor());
		Audio->SetLoop2D(TEXT("weather.wind"), WeatherSoundFile(TEXT("wind")),
			bOn && WeatherKind != EMRWeatherKind::None ? FMath::Clamp((WindAmount - 1.f) / 1.5f, 0.f, 1.f) * 0.7f * Inside : 0.f, !IsOutdoor());
	}
}

void UMREnvironmentSubsystem::TickLightning(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	for (int32 i = ThunderAt.Num() - 1; i >= 0; --i)
	{
		if (Now < ThunderAt[i])
		{
			continue;
		}
		ThunderAt.RemoveAt(i);
		UMRAudioSubsystem* Audio = bSoundsOn ? GetWorld()->GetSubsystem<UMRAudioSubsystem>() : nullptr;
		if (Audio)
		{
			Audio->PlayOriginal(WeatherSoundFile(TEXT("thunder")), FVector::ZeroVector, true,
				FMath::FRandRange(0.5f, 1.f) * (IsOutdoor() ? 1.f : 0.6f), FMath::FRandRange(0.85f, 1.1f), !IsOutdoor());
		}
	}
	const bool bCan = WeatherKind == EMRWeatherKind::Rain && StormAmount > 0.6f && !bNoLightning
		&& CVarLightning.GetValueOnGameThread() != 0;
	const TSharedPtr<FJsonObject>* WeatherCfg = nullptr;
	const TSharedPtr<FJsonObject>* LightningCfg = nullptr;
	const TSharedPtr<FJsonObject> Cfg = Root.IsValid() && Root->TryGetObjectField(TEXT("weather"), WeatherCfg)
		&& (*WeatherCfg)->TryGetObjectField(TEXT("lightning"), LightningCfg) ? *LightningCfg : nullptr;
	if (FlashStart < 0.0)
	{
		if (!bCan)
		{
			NextFlash = 0.0;
			return;
		}
		if (NextFlash == 0.0)
		{
			NextFlash = Now + (bHoldLightning ? 0.5 : FMath::FRandRange(Number(Cfg, TEXT("min_seconds"), 8.0), Number(Cfg, TEXT("max_seconds"), 30.0)));
		}
		if (Now < NextFlash)
		{
			return;
		}
		FlashStart = Now;
		FlashScale = bHoldLightning ? 1.f : FMath::FRandRange(0.5f, 1.f);
		NextFlash = 0.0;
		// the thunder follows, later the farther the stroke
		ThunderAt.Add(Now + FMath::FRandRange(0.4f, 3.5f));
		// a distant bolt in the sky, outdoors (M_Bolt: one of four, sometimes mirrored)
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (IsOutdoor() && PC && PC->PlayerCameraManager)
		{
			if (!BoltMID.IsValid())
			{
				if (UMaterialInterface* Bolt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Generated/Environment/Materials/MI_Bolt.MI_Bolt"), nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					BoltMID = UMaterialInstanceDynamic::Create(Bolt, this);
				}
			}
			if (BoltMID.IsValid())
			{
				BoltMID->SetScalarParameterValue(TEXT("Variant"), float(FMath::RandRange(0, 3)));
				BoltMID->SetScalarParameterValue(TEXT("Mirror"), FMath::RandBool() ? 1.f : 0.f);
				const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
				const double Yaw = FMath::FRandRange(0.0, 2.0 * PI);
				const double Dist = Number(Cfg, TEXT("bolt_distance_m"), 2200.0) * FMath::FRandRange(0.8, 1.2) * 100.0;
				const double Height = Number(Cfg, TEXT("bolt_height_m"), 900.0) * 100.0;
				const FVector At = Cam + FVector(FMath::Cos(Yaw) * Dist, FMath::Sin(Yaw) * Dist, Height * 0.5 - 2000.0);
				if (AMRPrecipitationActor* Actor = EnsurePrecip())
				{
					Actor->ShowBolt(BoltMID.Get(), At, float(Height * 0.28), float(Height));
				}
			}
		}
	}
	// a stroke: a bright flash, a flicker, a second flash, fading out over 0.7 s
	const double T = bHoldLightning ? 0.0 : Now - FlashStart;  // held at the first flash
	float Envelope = 0.f;
	if (T < 0.08) { Envelope = 1.f; }
	else if (T < 0.16) { Envelope = 0.25f; }
	else if (T < 0.24) { Envelope = 0.8f; }
	else if (T < 0.7) { Envelope = float(0.6 * (1.0 - (T - 0.24) / 0.46)); }
	else
	{
		FlashStart = -1.0;
		if (Precip.IsValid())
		{
			Precip->ShowBolt(nullptr, FVector::ZeroVector, 0.f, 0.f);
		}
	}
	Envelope *= FlashScale;
	if (BoltMID.IsValid())
	{
		BoltMID->SetScalarParameterValue(TEXT("Flash"), Envelope);
	}

	if (IsOutdoor())
	{
		if (!LightningLight.IsValid())
		{
			if (AActor* Actor = FindActor(TEXT("Lightning")))
			{
				LightningLight = Actor->FindComponentByClass<ULightComponent>();
			}
		}
		if (ULightComponent* Light = LightningLight.Get())
		{
			Light->SetIntensity(float(Number(Cfg, TEXT("lux"), 6.0)) * Envelope);
			Light->SetVisibility(Envelope > 0.f);
		}
	}
	else if (Collection)
	{
		// inside: the windows flash
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), Collection, TEXT("WindowDaylight"),
			BaseWindowDaylight + Envelope * float(Number(Cfg, TEXT("window_flash"), 2.0)) * float(Number(Profile, TEXT("daylight"), 1.0)));
	}
}

TStatId UMREnvironmentSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMREnvironmentSubsystem, STATGROUP_Tickables);
}

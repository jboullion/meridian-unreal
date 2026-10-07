#include "Monsters/MRMonsterSubsystem.h"

#include "Character/MRSpriteData.h"
#include "Components/CapsuleComponent.h"
#include "Core/MRUnits.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Monsters/MRMonster.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSpawn(TEXT("mr.Monster.Spawn"), 1,
		TEXT("Populate the zones with monsters and NPCs (server, when the world starts)."));

	TSharedPtr<FJsonObject> LoadJson(const FString& Name)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(UMRZoneSubsystem::GetDataDir(), Name)))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	// zones.json is an array at the top level
	TArray<TSharedPtr<FJsonValue>> LoadJsonArray(const FString& Name)
	{
		FString Text;
		TArray<TSharedPtr<FJsonValue>> Root;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(UMRZoneSubsystem::GetDataDir(), Name)))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	FAutoConsoleCommandWithWorld ResetCommand(TEXT("MRMonsterReset"), TEXT("Kill every monster and spawn the rooms again (server)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UMRMonsterSubsystem* M = World ? World->GetSubsystem<UMRMonsterSubsystem>() : nullptr)
			{
				M->Reset();
			}
		}));
}

bool UMRMonsterSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UMRMonsterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bStarted = InWorld.GetNetMode() == NM_Client || CVarSpawn.GetValueOnGameThread() == 0;  // clients get them replicated
}

void UMRMonsterSubsystem::Tick(float DeltaTime)
{
	if (!bStarted)
	{
		const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
		StartDelay -= DeltaTime;
		if (Zones && Zones->IsLoaded() && StartDelay <= 0.f)
		{
			bStarted = true;
			Start();
		}
		return;
	}
	for (FSpawnRoom& Room : Rooms)
	{
		Room.Alive.RemoveAll([](const TWeakObjectPtr<AMRMonster>& M) { return !M.IsValid() || M->IsDead(); });
		Room.Timer -= DeltaTime;
		if (Room.Timer <= 0.f)
		{
			Room.Timer = Room.GenSeconds;
			if (Room.Alive.Num() < Room.Max)
			{
				SpawnInRoom(Room);
			}
		}
	}
}

void UMRMonsterSubsystem::Start()
{
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const TSharedPtr<FJsonObject> Layout = LoadJson(TEXT("zone_layout.json"));
	TMap<int32, TSharedPtr<FJsonObject>> LayoutByRid;
	const TArray<TSharedPtr<FJsonValue>>* LayoutZones;
	if (Layout && Layout->TryGetArrayField(TEXT("zones"), LayoutZones))
	{
		for (const TSharedPtr<FJsonValue>& V : *LayoutZones)
		{
			LayoutByRid.Add(static_cast<int32>(V->AsObject()->GetNumberField(TEXT("rid"))), V->AsObject());
		}
	}
	SpawnNpcs(Layout);

	int32 Total = 0;
	for (const TSharedPtr<FJsonValue>& V : LoadJsonArray(TEXT("zones.json")))
	{
		const TSharedPtr<FJsonObject> Z = V->AsObject();
		const TSharedPtr<FJsonObject>* Spawning;
		if (!Z->TryGetObjectField(TEXT("spawning"), Spawning) || !Spawning->IsValid())
		{
			continue;
		}
		FSpawnRoom& Room = Rooms.AddDefaulted_GetRef();
		Room.Rid = static_cast<int32>(Z->GetNumberField(TEXT("rid")));
		for (const TSharedPtr<FJsonValue>& M : (*Spawning)->GetArrayField(TEXT("monsters")))
		{
			const FName Class(*M->AsObject()->GetStringField(TEXT("class")));
			if (FMRSpriteLibrary::Get().Monsters.Contains(Class))
			{
				Room.Classes.Add({Class, static_cast<float>(M->AsObject()->GetNumberField(TEXT("weight")))});
			}
		}
		(*Spawning)->TryGetNumberField(TEXT("init_count_min"), Room.InitMin);
		(*Spawning)->TryGetNumberField(TEXT("init_count_max"), Room.InitMax);
		(*Spawning)->TryGetNumberField(TEXT("monster_count_max"), Room.Max);
		double GenMs = 20000.0;
		(*Spawning)->TryGetNumberField(TEXT("gen_time_ms"), GenMs);
		Room.GenSeconds = static_cast<float>(GenMs / 1000.0);
		Room.Timer = Room.GenSeconds;
		const FMRZoneInfo* Info = Zones ? Zones->FindZone(Room.Rid) : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Gens;
		if (Info && LayoutByRid.Contains(Room.Rid) && LayoutByRid[Room.Rid]->TryGetArrayField(TEXT("generators"), Gens))
		{
			for (const TSharedPtr<FJsonValue>& G : *Gens)
			{
				const TArray<TSharedPtr<FJsonValue>>& P = G->AsObject()->GetArrayField(TEXT("pos"));
				Room.Generators.Add(Info->Origin + MRUnits::LayoutToLocal(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()));
			}
		}
		if (Room.Classes.Num() == 0 || !Info)
		{
			Rooms.Pop();
			continue;
		}
		const int32 Count = FMath::RandRange(Room.InitMin, FMath::Max(Room.InitMin, Room.InitMax));
		for (int32 i = 0; i < Count; ++i)
		{
			SpawnInRoom(Room);
		}
		Total += Room.Alive.Num();
	}
	UE_LOG(LogMeridian, Log, TEXT("Monsters: %d spawned in %d rooms, %d alive in all"), Total, Rooms.Num(), NumAlive());
}

void UMRMonsterSubsystem::SpawnNpcs(const TSharedPtr<FJsonObject>& Layout)
{
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const TArray<TSharedPtr<FJsonValue>>* LayoutZones;
	if (!Zones || !Layout || !Layout->TryGetArrayField(TEXT("zones"), LayoutZones))
	{
		return;
	}
	int32 Count = 0;
	for (const TSharedPtr<FJsonValue>& V : *LayoutZones)
	{
		const TSharedPtr<FJsonObject> Z = V->AsObject();
		const int32 Rid = static_cast<int32>(Z->GetNumberField(TEXT("rid")));
		const FMRZoneInfo* Info = Zones->FindZone(Rid);
		const TArray<TSharedPtr<FJsonValue>>* Objects;
		if (!Info || !Z->TryGetArrayField(TEXT("objects"), Objects))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& O : *Objects)
		{
			const FName Class(*O->AsObject()->GetStringField(TEXT("class")));
			const FMRMonsterDef* Def = FMRSpriteLibrary::Get().Monsters.Find(Class);
			if (!Def || !Def->bNpc)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>& P = O->AsObject()->GetArrayField(TEXT("pos"));
			const FVector At = Info->Origin + MRUnits::LayoutToLocal(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber());
			double KodYaw = 0.0;
			const bool bYaw = O->AsObject()->TryGetNumberField(TEXT("yaw_kod"), KodYaw);
			if (SpawnMonster(Class, Rid, At, bYaw ? MRUnits::KodAngleToYaw(static_cast<int32>(KodYaw)) : 0.f, false))
			{
				++Count;
			}
		}
	}
	UE_LOG(LogMeridian, Log, TEXT("Monsters: %d NPCs placed"), Count);
}

void UMRMonsterSubsystem::SpawnInRoom(FSpawnRoom& Room)
{
	float Sum = 0.f;
	for (const TPair<FName, float>& C : Room.Classes)
	{
		Sum += C.Value;
	}
	float Pick = FMath::FRand() * Sum;
	FName Class = Room.Classes[0].Key;
	for (const TPair<FName, float>& C : Room.Classes)
	{
		if ((Pick -= C.Value) <= 0.f)
		{
			Class = C.Key;
			break;
		}
	}
	FVector Near;
	if (Room.Generators.Num() > 0)
	{
		Near = Room.Generators[FMath::RandRange(0, Room.Generators.Num() - 1)];
	}
	else
	{
		// anywhere in the room's grid; FindFloor checks it's a floor with room to stand
		const FMRZoneInfo* Info = GetWorld()->GetSubsystem<UMRZoneSubsystem>()->FindZone(Room.Rid);
		Near = Info->Origin + MRUnits::RooToLocal(FVector2D(FMath::FRand() * Info->GridSizeRoo.X, FMath::FRand() * Info->GridSizeRoo.Y));
	}
	if (AMRMonster* M = SpawnMonster(Class, Room.Rid, Near, FMath::FRand() * 360.f, true))
	{
		Room.Alive.Add(M);
	}
}

bool UMRMonsterSubsystem::FindFloor(int32 Rid, const FVector& Near, float Jitter, float HalfHeight, float Radius, FVector& Out) const
{
	const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	// no jitter (an NPC): only Kod's spot
	for (int32 Try = 0; Try < (Jitter > 0.f ? 24 : 1); ++Try)
	{
		FVector P = Near;
		if (Try > 0 || Jitter > 0.f)
		{
			const float R = Try == 0 ? Jitter : Jitter + Try * 40.f;
			P += FVector(FMath::FRandRange(-R, R), FMath::FRandRange(-R, R), 0.f);
		}
		if (!Zones->TraceFloor(P, true))  // the lowest floor: not on a roof we added
		{
			continue;
		}
		const FVector Centre = P + FVector(0.f, 0.f, HalfHeight + 2.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MRMonsterSpawn), false);
		if (GetWorld()->OverlapAnyTestByChannel(Centre, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
		{
			continue;  // inside a wall, a prop or another monster
		}
		Out = Centre;
		return true;
	}
	return false;
}

AMRMonster* UMRMonsterSubsystem::SpawnMonster(FName Class, int32 Zone, const FVector& Near, float Yaw, bool bRandomise)
{
	UWorld* World = GetWorld();
	const FTransform Initial(FRotator(0.f, Yaw, 0.f), Near + FVector(0.f, 0.f, 200.f));
	AMRMonster* M = World->SpawnActorDeferred<AMRMonster>(AMRMonster::StaticClass(), Initial, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!M)
	{
		return nullptr;
	}
	M->InitMonster(Class, Zone);  // sizes the capsule from its look
	const UCapsuleComponent* Capsule = M->GetCapsuleComponent();
	// its own capsule (already registered, up at Initial) mustn't block its floor trace or overlap test
	M->SetActorEnableCollision(false);
	FVector At;
	if (!FindFloor(Zone, Near, bRandomise ? 100.f : 0.f, Capsule->GetUnscaledCapsuleHalfHeight(), Capsule->GetUnscaledCapsuleRadius(), At))
	{
		if (bRandomise)
		{
			M->Destroy();
			return nullptr;
		}
		// an NPC stays where Kod put it even if a counter is in the way: on the traced floor
		At = Near;
		GetWorld()->GetSubsystem<UMRZoneSubsystem>()->TraceFloor(At, true);
		At.Z += Capsule->GetUnscaledCapsuleHalfHeight() + 2.f;
	}
	M->FinishSpawning(FTransform(FRotator(0.f, Yaw, 0.f), At));
	M->SetActorEnableCollision(true);
	return M;
}

void UMRMonsterSubsystem::Reset()
{
	for (TActorIterator<AMRMonster> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	Rooms.Reset();
	if (GetWorld()->GetNetMode() != NM_Client)
	{
		Start();
	}
}

int32 UMRMonsterSubsystem::NumAlive() const
{
	int32 N = 0;
	for (TActorIterator<AMRMonster> It(GetWorld()); It; ++It)
	{
		N += It->IsDead() ? 0 : 1;
	}
	return N;
}

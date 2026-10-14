#include "Ambient/MRAmbientSubsystem.h"

#include "Ambient/MRAmbientFigure.h"
#include "Ambient/MRNatureActor.h"
#include "Net/MRSocialClient.h"
#include "Character/MRCharacter.h"
#include "Core/MRUnits.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Environment/MREnvironmentSubsystem.h"
#include "Environment/MRGameTimeSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Monsters/MRMonster.h"
#include "Net/MRAssetCache.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Player/MRPlayerState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMeridian.h"
#include "Zones/MRZoneSubsystem.h"

using namespace MRAmbient;

namespace
{
	TAutoConsoleVariable<int32> CVarAmbientLevel(TEXT("mr.Ambient.Level"), 2,
		TEXT("Ambient life (docs/adr/0013): the townsfolk, patrons, courtiers and customers only this client sees. 0 off, 1 few, 2 normal, 3 many."));
	TAutoConsoleVariable<int32> CVarNature(TEXT("mr.Ambient.Nature"), 1,
		TEXT("Ambient life's butterflies over green country by day and fish leaping from open water (docs/adr/0013). 0 off."));
	TAutoConsoleVariable<int32> CVarFlyers(TEXT("mr.Ambient.Flyers"), 1,
		TEXT("Ambient life's flocks of birds overhead by day and bats in caves, dungeons, crypts and prisons (docs/adr/0013). 0 off."));
	TAutoConsoleVariable<int32> CVarDebug(TEXT("mr.Ambient.Debug"), 0,
		TEXT("Log ambient life's counts every few seconds: people, critters, butterflies, birds, bats, water found."));
	/** Prisons (no terrain of their own): the old jailhouse of Barloque and Bouldergate's (critters.ts PRISONS) */
	const TCHAR* Prisons[] = {TEXT("barjail.roo"), TEXT("berg_jail.roo")};
	/** Shards' ambientIds.ts AMBIENT_ID_BASE: above any 28-bit Kod object id */
	TAutoConsoleVariable<int32> CVarCritters(TEXT("mr.Ambient.Critters"), 1,
		TEXT("Ambient life's critters (docs/adr/0013): rats, spiders, centipedes, ants, scorpions and bunnies about the player, by the room's terrain. 0 off."));
	constexpr uint32 AmbientIdBase = 0x40000000u;
	/** proto.h MOVEON_NO: an object that blocks the way */
	constexpr uint8 MoveOnNo = 1;
	/** A figure's floor is looked for again this often while it walks, s */
	constexpr double FloorEvery = 0.2;

	FString NavName(const FString& RoomFile)
	{
		return FPaths::GetBaseFilename(RoomFile).ToLower() + TEXT(".nav");
	}
}

bool UMRAmbientSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UMRAmbientSubsystem::Deinitialize()
{
	Reset();
	if (IsValid(Nature))
	{
		Nature->Destroy();
	}
	Nature = nullptr;
	if (Social.IsValid())
	{
		Social->OnArena.Unbind();
		Social->Close();
		Social.Reset();
	}
	Super::Deinitialize();
}

TStatId UMRAmbientSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRAmbientSubsystem, STATGROUP_Tickables);
}

double UMRAmbientSubsystem::LevelScale()
{
	switch (FMath::Clamp(CVarAmbientLevel.GetValueOnGameThread(), 0, 3))
	{
	case 1: return 0.5;
	case 2: return 1.0;
	case 3: return 2.0;
	default: return 0.0;
	}
}

void UMRAmbientSubsystem::Reset()
{
	Life.Reset();
	Critters.Reset();
	Arena.Reset();
	if (Social.IsValid())
	{
		Social->WatchArena(FString(), -1.0);
	}
	if (IsValid(Nature))
	{
		Nature->Clear();
	}
	Terrain.Reset();
	BatsKind.Reset();
	SkyAge = 10.0;
	for (const TPair<uint32, TObjectPtr<AMRAmbientFigure>>& F : Figures)
	{
		if (IsValid(F.Value))
		{
			F.Value->Destroy();
		}
	}
	Figures.Reset();
	Shown.Reset();
	Rid = 0;
	RoomFile.Reset();
	bLoading = false;
}

bool UMRAmbientSubsystem::CurrentRoom(int32& OutRid, FString& OutFile) const
{
	const UWorld* World = GetWorld();
	const UMRZoneSubsystem* Zones = World ? World->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!Zones)
	{
		return false;
	}
	const UMRNetWorldSubsystem* NetWorld = World->GetSubsystem<UMRNetWorldSubsystem>();
	const UMRNetSubsystem* Net = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UMRNetSubsystem>() : nullptr;
	if (NetWorld && NetWorld->IsActive() && Net)
	{
		// online: the server's room (a zone we built from the same file, or one built at runtime)
		OutRid = NetWorld->GetRid();
		OutFile = Net->GetPlayer().RoomFile.ToLower();
		return OutRid > 0 && !OutFile.IsEmpty() && NetWorld->GetLoadingRoom().IsEmpty();
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	const AMRPlayerState* PS = PC ? PC->GetPlayerState<AMRPlayerState>() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	OutRid = PS && PS->GetZoneId() > 0 ? PS->GetZoneId() : (Pawn ? Zones->ZoneAtLocation(Pawn->GetActorLocation()) : 0);
	OutFile = Zones->GetRoomFile(OutRid);
	return OutRid > 0 && !OutFile.IsEmpty();
}

void UMRAmbientSubsystem::Tick(float DeltaTime)
{
	const double Scale = LevelScale();
	int32 NowRid = 0;
	FString NowFile;
	const bool bRoom = Scale > 0.0 && CurrentRoom(NowRid, NowFile);
	if (!bRoom)
	{
		if (Rid || Life.IsValid())
		{
			Reset();
		}
		return;
	}
	if (NowRid != Rid || NowFile != RoomFile)
	{
		Reset();
		StartRoom(NowRid, NowFile);
	}
	const double DtMs = FMath::Min(DeltaTime, 0.25f) * 1000.0;
	if (Critters.IsValid())
	{
		if (CVarCritters.GetValueOnGameThread() == 0)
		{
			Critters.Reset();
		}
		else if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->GetPawn())
		{
			FMRCritters::FFrame F;
			F.bHasPlayer = true;
			F.Player = PC->GetPawn()->GetActorLocation() - FVector(0.0, 0.0, PC->GetPawn()->GetSimpleCollisionHalfHeight());
			F.FacingYaw = FRotator::ClampAxis(PC->GetControlRotation().Yaw);
			F.Scale = Scale;
			if (const UMRGameTimeSubsystem* Time = GetWorld()->GetSubsystem<UMRGameTimeSubsystem>())
			{
				F.Hour = Time->GetGameHour();
			}
			Critters->Update(DtMs, F);
		}
	}
	StepNature(DeltaTime, Scale);
	if (Arena.IsValid())
	{
		if (const UMRGameTimeSubsystem* Time = GetWorld()->GetSubsystem<UMRGameTimeSubsystem>(); Time && Social.IsValid())
		{
			Social->SetHour(Time->GetGameHour());
		}
		Arena->Update(FPlatformTime::Seconds() * 1000.0, DtMs);
	}
	if (!Life.IsValid())
	{
		return;
	}
	Life->Update(DtMs, MakeFrame());
	Sync(DeltaTime);
}

void UMRAmbientSubsystem::StartRoom(int32 InRid, const FString& InFile)
{
	Rid = InRid;
	RoomFile = InFile;
	// the room's terrain (data/zones.json; a room built at runtime: by its file alone), for critters and nature
	{
		const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
		const FMRZoneInfo* Zone = Zones ? Zones->FindZone(InRid) : nullptr;
		Terrain = Zone ? Zone->Terrain : TArray<FString>();
		// critters.ts batsOf: caves and dungeons under their roofs, crypts, Brax and prisons
		FMRCritters::FHabitat H;
		const bool bHabitat = FMRCritters::HabitatOf(InFile, Terrain, H);
		BatsKind = bHabitat && (H.Name == TEXT("cave") || H.Name == TEXT("dungeon")) ? TEXT("covered")
			: (Terrain.Contains(TEXT("NECROPOLIS")) || InFile == Prisons[0] || InFile == Prisons[1]) ? TEXT("necropolis") : TEXT("");
	}
	if (CVarCritters.GetValueOnGameThread() != 0)
	{
		const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
		const FMRZoneInfo* Zone = Zones ? Zones->FindZone(InRid) : nullptr;
		FMRCritters::FHabitat Habitat;
		if (FMRCritters::HabitatOf(InFile, Zone ? Zone->Terrain : TArray<FString>(), Habitat))
		{
			Critters = MakeUnique<FMRCritters>(GetWorld(), Habitat, FMath::Rand32() ^ (Visits * 2654435761u), AmbientIdBase + 0x00800000u + Visits * 4096u);
			UE_LOG(LogMeridian, Log, TEXT("MRAmbient: %s: critters of the %s"), *InFile, *Habitat.Name);
		}
	}
	const FRoomSpec* Spec = FData::Get().Room(InFile);
	if (Spec && Spec->bArena)
	{
		StartArena();  // the arena's floor: the relay's fights
		return;
	}
	if (!Spec)
	{
		return;  // nobody here
	}
	// the rooms we built: data/ambient (tools/world/ambient_data.py), the same files as Shards' asset build
	const FString Dir = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ambient"));
	FMRNavGrid Grid;
	TArray<uint8> Bytes;
	const bool bLocalNav = FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Dir, TEXT("nav"), NavName(InFile)), FILEREAD_Silent) && Grid.Decode(Bytes);
	// online, a room built at runtime may be another version of ours: its own grid then
	const UMRNetSubsystem* Net = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UMRNetSubsystem>() : nullptr;
	const UMRNetWorldSubsystem* NetWorld = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	const bool bOnline = NetWorld && NetWorld->IsActive() && Net;
	const bool bGridMatches = bLocalNav && (!bOnline || MRNetRead::SecurityMatches(Grid.Security, Net->GetPlayer().RoomSecurity));
	if (!ExitsJson.IsValid())
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("exits.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
		{
			const TSharedPtr<FJsonObject>* Rooms = nullptr;
			if (Root->TryGetObjectField(TEXT("rooms"), Rooms))
			{
				ExitsJson = *Rooms;
			}
		}
	}
	FMRAmbientLife::FExits Exits;
	const bool bLocalExits = ReadExits(ExitsJson, InFile, Exits);
	if (bGridMatches && (bLocalExits || !bOnline))
	{
		Begin(Grid, Exits);
		return;
	}
	FMRAssetCache* Cache = bOnline ? Net->GetAssets() : nullptr;
	if (!Cache || !Cache->IsListed(TEXT("nav/") + NavName(InFile)))
	{
		if (bLocalNav)
		{
			Begin(Grid, Exits);
		}
		else
		{
			UE_LOG(LogMeridian, Log, TEXT("MRAmbient: no nav grid for %s: nobody about"), *InFile);
		}
		return;
	}
	// the server's own: its grid, and (once) every room's doors
	bLoading = true;
	TWeakObjectPtr<UMRAmbientSubsystem> Weak(this);
	const FString Want = InFile;
	Cache->Fetch(TEXT("nav/") + NavName(InFile), [Weak, Want, Cache](bool bOk, const TArray<uint8>& NavBytes)
	{
		UMRAmbientSubsystem* Self = Weak.Get();
		if (!Self || Self->RoomFile != Want)
		{
			return;
		}
		FMRNavGrid G;
		if (!bOk || !G.Decode(NavBytes))
		{
			Self->bLoading = false;
			UE_LOG(LogMeridian, Warning, TEXT("MRAmbient: the server's nav grid for %s didn't load"), *Want);
			return;
		}
		const auto Go = [Weak, Want, G]()
		{
			UMRAmbientSubsystem* S = Weak.Get();
			if (S && S->RoomFile == Want)
			{
				FMRAmbientLife::FExits E;
				S->ReadExits(S->ExitsJson, Want, E);
				S->bLoading = false;
				S->Begin(G, E);
			}
		};
		if (Self->bExitsFromCache || !Cache->IsListed(TEXT("roomexits.json")))
		{
			Go();
			return;
		}
		Cache->Fetch(TEXT("roomexits.json"), [Weak, Go](bool bJson, const TArray<uint8>& JsonBytes)
		{
			if (UMRAmbientSubsystem* S = Weak.Get(); S && bJson)
			{
				FString Text;
				FFileHelper::BufferToString(Text, JsonBytes.GetData(), JsonBytes.Num());
				TSharedPtr<FJsonObject> Root;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
				{
					S->ExitsJson = Root;
					S->bExitsFromCache = true;
				}
			}
			Go();
		});
	});
}

bool UMRAmbientSubsystem::ReadExits(const TSharedPtr<FJsonObject>& Json, const FString& File, FMRAmbientLife::FExits& Out) const
{
	// Shards' roomexits.json: {"doors": [[row, col, to|null]...], "edges": [{"side", "to"}...], "arrivals": [[row, col]...]}
	Out = FMRAmbientLife::FExits();
	const TSharedPtr<FJsonObject>* Room = nullptr;
	if (!Json.IsValid() || !Json->TryGetObjectField(File.ToLower(), Room))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if ((*Room)->TryGetArrayField(TEXT("doors"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TArray<TSharedPtr<FJsonValue>>& D = V->AsArray();
			if (D.Num() >= 2)
			{
				FMRAmbientLife::FExits::FDoor Door;
				Door.Row = int32(D[0]->AsNumber());
				Door.Col = int32(D[1]->AsNumber());
				if (D.Num() > 2 && D[2]->Type == EJson::String)
				{
					Door.To = D[2]->AsString();
				}
				Out.Doors.Add(Door);
			}
		}
	}
	if ((*Room)->TryGetArrayField(TEXT("edges"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			FMRAmbientLife::FExits::FEdge Edge;
			const FString Side = E.IsValid() ? E->GetStringField(TEXT("side")) : FString();
			Edge.Side = Side.Len() ? Side[0] : TEXT('N');
			if (E.IsValid())
			{
				E->TryGetStringField(TEXT("to"), Edge.To);
			}
			Out.Edges.Add(Edge);
		}
	}
	if ((*Room)->TryGetArrayField(TEXT("arrivals"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			if (A.Num() >= 2)
			{
				Out.Arrivals.Emplace(int32(A[0]->AsNumber()), int32(A[1]->AsNumber()));
			}
		}
	}
	return true;
}

void UMRAmbientSubsystem::Begin(const FMRNavGrid& Grid, const FMRAmbientLife::FExits& Exits)
{
	const FRoomSpec* Spec = FData::Get().Room(RoomFile);
	if (!Spec || !Grid.IsValid())
	{
		return;
	}
	uint32 Seed = FMath::Rand32();
	if (int32 Fixed = 0; FParse::Value(FCommandLine::Get(), TEXT("MRAmbientSeed="), Fixed))
	{
		Seed = uint32(Fixed) * 7919u + Visits;
	}
	++Visits;
	Life = MakeUnique<FMRAmbientLife>(*Spec, Exits, Grid, Seed, AmbientIdBase + Visits * 4096u);
	UE_LOG(LogMeridian, Log, TEXT("MRAmbient: %s (zone %d): a crowd of %d, %d doors, nav %dx%d"), *RoomFile, Rid, Spec->Count, Exits.Doors.Num(), Grid.Cols, Grid.Rows);
}

FMRAmbientLife::FFrame UMRAmbientSubsystem::MakeFrame() const
{
	FMRAmbientLife::FFrame F;
	F.Scale = LevelScale();
	UWorld* World = GetWorld();
	const UMRZoneSubsystem* Zones = World->GetSubsystem<UMRZoneSubsystem>();
	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn && Zones)
	{
		F.bHasPlayer = true;
		F.Player = Zones->WorldToRoo(Rid, Pawn->GetActorLocation());
		const float Yaw = PC->GetControlRotation().Yaw;
		F.Facing = FMath::RoundToInt(FRotator::ClampAxis(Yaw) * 4096.0 / 360.0) & 4095;
		if (const AMRCharacter* Char = Cast<AMRCharacter>(Pawn))
		{
			F.bImpressive = IsImpressiveBody(Char->GetSpriteAppearance().BodyBgf.ToString());
		}
	}
	if (const UMRGameTimeSubsystem* Time = World->GetSubsystem<UMRGameTimeSubsystem>())
	{
		F.Hour = Time->GetGameHour();
	}
	if (const UMREnvironmentSubsystem* Env = World->GetSubsystem<UMREnvironmentSubsystem>(); Env && Env->GetStormAmount() > 0.3f)
	{
		F.Weather = Env->GetWeatherKind() == EMRWeatherKind::Snow ? EWeather::Snow : Env->GetWeatherKind() == EMRWeatherKind::Rain ? EWeather::Rain : EWeather::None;
	}
	// what stands in the way: online the server's objects that block (MOVEON_NO), but players; offline the NPCs
	const UMRNetWorldSubsystem* NetWorld = World->GetSubsystem<UMRNetWorldSubsystem>();
	const UMRNetSubsystem* Net = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UMRNetSubsystem>() : nullptr;
	if (NetWorld && NetWorld->IsActive() && Net)
	{
		for (const TPair<uint32, FMRNetObject>& O : Net->GetNetWorld().Objects)
		{
			if (O.Value.MoveOn == MoveOnNo && !O.Value.IsPlayer())
			{
				F.Obstacles.Emplace((O.Value.KodCol - MRUnits::KodPerSquare) * 16.0, (O.Value.KodRow - MRUnits::KodPerSquare) * 16.0);
			}
		}
	}
	else if (Zones)
	{
		for (TActorIterator<AMRMonster> It(World); It; ++It)
		{
			if (!It->IsDead())
			{
				F.Obstacles.Add(Zones->WorldToRoo(Rid, It->GetActorLocation()));
			}
		}
	}
	return F;
}

FVector UMRAmbientSubsystem::FloorAt(const FVector2D& Roo, FShown& S, bool bForce) const
{
	const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	if (!Zones)
	{
		return FVector::ZeroVector;
	}
	if (!bForce && S.Roo.X >= 0.0)
	{
		// walking on: a short look down from knee height, the floor it was on near
		const FVector Flat = Zones->RooToWorld(Rid, Roo, false);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MRAmbientFloor), false);
		FCollisionResponseParams WorldOnly;
		WorldOnly.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
		const FVector From(Flat.X, Flat.Y, S.Feet.Z + 60.0), To(Flat.X, Flat.Y, S.Feet.Z - 150.0);
		if (GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_WorldStatic, Params, WorldOnly))
		{
			return Hit.ImpactPoint;
		}
	}
	return Zones->RooToWorld(Rid, Roo, true);
}

void UMRAmbientSubsystem::Sync(float DeltaTime)
{
	UWorld* World = GetWorld();
	TSet<uint32> Seen;
	for (const FMRAmbientLife::FWalker& W : Life->GetWalkers())
	{
		Seen.Add(W.Id);
		FShown& S = Shown.FindOrAdd(W.Id);
		TObjectPtr<AMRAmbientFigure>& Fig = Figures.FindOrAdd(W.Id);
		const bool bNew = !IsValid(Fig);
		if (bNew)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			Fig = World->SpawnActor<AMRAmbientFigure>(AMRAmbientFigure::StaticClass(), FTransform::Identity, Params);
			if (!Fig)
			{
				continue;
			}
			Fig->AmbientId = W.Id;
#if WITH_EDITOR
			Fig->SetActorLabel(FString::Printf(TEXT("Ambient_%s"), *W.Name.Replace(TEXT(" "), TEXT("_"))));
#endif
		}
		if (bNew || W.bTorch != S.bTorch)
		{
			FMRSpriteAppearance A;
			if (ToAppearance(W.Look, W.bTorch, A))
			{
				Fig->SetPerson(A);
			}
			S.bTorch = W.bTorch;
		}
		// where: on the floor under its ROO point, looked for again while it moves
		const bool bMoved = FVector2D::Distance(S.Roo, W.Pos) > 1.0;
		S.FloorAge += DeltaTime;
		FVector Feet = S.Feet;
		if (bNew || (bMoved && S.FloorAge >= FloorEvery) || S.Roo.X < 0.0)
		{
			Feet = FloorAt(W.Pos, S, bNew);
			S.FloorAge = 0.0;
		}
		else if (bMoved)
		{
			const FVector Flat = World->GetSubsystem<UMRZoneSubsystem>()->RooToWorld(Rid, W.Pos, false);
			Feet = FVector(Flat.X, Flat.Y, S.Feet.Z);
		}
		const FVector Velocity = !bNew && DeltaTime > 0.f && W.bWalking ? (Feet - S.Feet) / DeltaTime * FVector(1.0, 1.0, 0.0) : FVector::ZeroVector;
		S.Feet = Feet;
		S.Roo = W.Pos;
		Fig->Place(Feet, W.Angle * 360.f / 4096.f, Velocity);
		Fig->SetFade(float(W.Fade));
		// an emote starting (again): the sprite body's action; standing or walking ends one that loops
		if (W.PoseSerial != S.PoseSerial)
		{
			S.PoseSerial = W.PoseSerial;
			Fig->PlayAction(PoseAction(W.Pose));
		}
	}
	for (auto It = Figures.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			if (IsValid(It.Value()))
			{
				It.Value()->Destroy();
			}
			Shown.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void UMRAmbientSubsystem::OnPlayerWave()
{
	if (!Life.IsValid())
	{
		return;
	}
	const FMRAmbientLife::FFrame F = MakeFrame();
	if (F.bHasPlayer)
	{
		Life->WavedAt(F.Player, F.Facing);
	}
}

void UMRAmbientSubsystem::OnSpell(const FVector& World, double RadiusCm)
{
	const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	if (Life.IsValid() && Zones)
	{
		Life->Scatter(Zones->WorldToRoo(Rid, World), RadiusCm / MRUnits::CmPerSquare * 1024.0);
	}
}

TArray<UMRAmbientSubsystem::FSpeech> UMRAmbientSubsystem::GetSpeech() const
{
	TArray<FSpeech> Out;
	if (Arena.IsValid())
	{
		for (const FMRArenaPlayback::FSpeech& S : Arena->GetSpeech())
		{
			Out.Add(FSpeech{S.Anchor, S.Text});
		}
	}
	if (!Life.IsValid())
	{
		return Out;
	}
	for (const FMRAmbientLife::FWalker& W : Life->GetWalkers())
	{
		const TObjectPtr<AMRAmbientFigure>* Fig = Figures.Find(W.Id);
		if (!W.Say.IsEmpty() && W.FadeDir >= 0 && Fig && IsValid(*Fig))
		{
			Out.Add(FSpeech{(*Fig)->GetSpeechAnchor(), W.Say});
		}
	}
	return Out;
}

uint32 UMRAmbientSubsystem::FigureNear(const FVector& RayStart, const FVector& RayDir, double MaxDistCm) const
{
	if (Arena.IsValid())
	{
		if (const uint32 Id = Arena->FigureNear(RayStart, RayDir))
		{
			return Id | 0x80000000u;  // the arena's (Inspect asks it)
		}
	}
	if (!Life.IsValid())
	{
		return 0;
	}
	uint32 Best = 0;
	double BestOff = 60.0;  // cm off the ray, at most
	for (const TPair<uint32, TObjectPtr<AMRAmbientFigure>>& F : Figures)
	{
		if (!IsValid(F.Value) || !Life->Has(F.Key))
		{
			continue;
		}
		// the ray against the figure's middle, a person's height up
		const FVector Mid = F.Value->GetFeet() + FVector(0.0, 0.0, 90.0);
		const double Along = FVector::DotProduct(Mid - RayStart, RayDir);
		if (Along <= 0.0 || Along > MaxDistCm)
		{
			continue;
		}
		const double Off = FVector::Dist(RayStart + RayDir * Along, Mid);
		if (Off < BestOff)
		{
			BestOff = Off;
			Best = F.Key;
		}
	}
	return Best;
}

bool UMRAmbientSubsystem::Describe(uint32 Id, FMRAmbientLife::FPersonInfo& Out) const
{
	return Life.IsValid() && Life->Describe(Id, Out);
}

bool UMRAmbientSubsystem::Inspect(uint32 Id, FString& OutName, FString& OutText, FMRSpriteAppearance& OutLook) const
{
	if (Id & 0x80000000u)
	{
		return Arena.IsValid() && Arena->Inspect(Id & 0x7FFFFFFFu, OutName, OutText, OutLook);
	}
	FMRAmbientLife::FPersonInfo Info;
	if (!Life.IsValid() || !Life->Describe(Id, Info))
	{
		return false;
	}
	const FMRAmbientLife::FWalker* W = Life->GetWalkers().FindByPredicate([Id](const FMRAmbientLife::FWalker& V) { return V.Id == Id; });
	if (!W || !ToAppearance(W->Look, W->bTorch, OutLook))
	{
		return false;
	}
	// Shards' AmbientPersonDialog: role, doing, where from and to (the rooms' names), mood
	const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const auto RoomName = [Zones](const FString& File) -> FString
	{
		const FMRZoneInfo* Z = Zones && !File.IsEmpty() ? Zones->FindZone(Zones->RidForRoom(File)) : nullptr;
		return Z ? Z->Name : FString();
	};
	OutName = Info.Name;
	TArray<FString> Lines;
	Lines.Add(Info.Role);
	Lines.Add(Info.Doing);
	if (const FString From = RoomName(Info.From); !From.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Came from: %s"), *From));
	}
	if (const FString Errand = RoomName(Info.Errand); !Errand.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("On an errand to: %s"), *Errand));
	}
	else if (const FString To = RoomName(Info.To); !To.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Going to: %s"), *To));
	}
	Lines.Add(FString::Printf(TEXT("Mood: %s"), *Info.Mood));
	OutText = FString::Join(Lines, TEXT("\n"));
	return true;
}

bool UMRAmbientSubsystem::SquishCritter(const FVector& RayStart, const FVector& RayDir)
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Critters.IsValid() || !Pawn)
	{
		return false;
	}
	// Shards: a click on one (Select Target with nothing real under the cursor), or an attack with
	// nothing real in reach: the nearest within 1.6 squares, ahead of us
	if (!RayDir.IsNearlyZero() && Critters->SquishOnRay(RayStart, RayDir, 6.0 * MRUnits::CmPerSquare))
	{
		return true;
	}
	const FVector Feet = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
	return Critters->SquishAhead(Feet, PC->GetControlRotation().Yaw, 1.6 * MRUnits::CmPerSquare);
}

void UMRAmbientSubsystem::StepNature(float DeltaTime, double Scale)
{
	// Shards' gameScene updateNature and updateFlyers: what's about by the room's terrain, the hour and the rain
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const double NatureScale = CVarNature.GetValueOnGameThread() ? Scale : 0.0;
	const double FlyerScale = CVarFlyers.GetValueOnGameThread() ? Scale : 0.0;
	if (NatureScale <= 0.0 && FlyerScale <= 0.0)
	{
		if (IsValid(Nature))
		{
			Nature->Clear();
		}
		return;
	}
	if (!IsValid(Nature))
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Nature = GetWorld()->SpawnActor<AMRNatureActor>(AMRNatureActor::StaticClass(), FTransform::Identity, Params);
		if (!Nature)
		{
			return;
		}
	}
	const FVector Feet = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
	// open sky over the player (Shards: any sector of the room without a ceiling), looked at now and then
	if ((SkyAge += DeltaTime) > 1.0)
	{
		SkyAge = 0.0;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MRAmbientSky), false, Pawn);
		bSky = !GetWorld()->LineTraceSingleByChannel(Hit, Feet + FVector(0.0, 0.0, 200.0), Feet + FVector(0.0, 0.0, 6000.0), ECC_WorldStatic, Params);
	}
	const auto Has = [this](std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* N : Names)
		{
			if (Terrain.Contains(N))
			{
				return true;
			}
		}
		return false;
	};
	const UMRGameTimeSubsystem* Time = GetWorld()->GetSubsystem<UMRGameTimeSubsystem>();
	const double Hour = Time ? Time->GetGameHour() : 12.0;
	const bool bDay = Hour >= 7.0 && Hour < 19.0;
	const bool bNight = Hour >= 20.0 || Hour < 5.0;
	const UMREnvironmentSubsystem* Env = GetWorld()->GetSubsystem<UMREnvironmentSubsystem>();
	const bool bRaining = Env && Env->GetStormAmount() > 0.3f && Env->GetWeatherKind() == EMRWeatherKind::Rain;
	const bool bGreen = Has({TEXT("FOREST"), TEXT("FIELDS"), TEXT("HILLS"), TEXT("JUNGLE")});
	const bool bCave = BatsKind == TEXT("covered") || (BatsKind == TEXT("necropolis") && !bSky);
	AMRNatureActor::FOptions O;
	O.Butterflies = bGreen && bDay && !bRaining ? FMath::RoundToInt(8.0 * NatureScale) : 0;
	O.bFish = NatureScale > 0.0 && !Has({TEXT("SEWERS"), TEXT("CAVES"), TEXT("MINES")});
	O.Flocks = bSky && !bCave && !bNight ? FMath::RoundToInt(2.0 * FlyerScale) : 0;
	O.Bats = bCave ? FMath::RoundToInt(9.0 * FlyerScale) : (!BatsKind.IsEmpty() && bNight ? FMath::RoundToInt(3.0 * FlyerScale) : 0);
	O.bCave = bCave;
	Nature->Step(DeltaTime, Feet, FRotator::ClampAxis(PC->GetControlRotation().Yaw), O);
	static double LastLog = 0.0;
	if (CVarDebug.GetValueOnGameThread() && FPlatformTime::Seconds() - LastLog > 3.0)
	{
		LastLog = FPlatformTime::Seconds();
		UE_LOG(LogMeridian, Log, TEXT("MRAmbient: %s hour %.1f sky %d: %d people, %d critters, butterflies %d/%d, birds %d (%d flocks wanted), bats %d/%d, water spots %d"),
			*RoomFile, Hour, bSky ? 1 : 0, Life.IsValid() ? Life->GetWalkers().Num() : 0, NumCritters(), Nature->NumButterflies(), O.Butterflies,
			Nature->NumBirds(), O.Flocks, Nature->NumBats(), O.Bats, Nature->NumWaterSpots());
	}
}

void UMRAmbientSubsystem::StartArena()
{
	// Meridian Shards' relay runs the arena (its ADR 0007): watch it, as its browser clients do
	UWorld* World = GetWorld();
	const UMRNetWorldSubsystem* NetWorld = World->GetSubsystem<UMRNetWorldSubsystem>();
	UMRNetSubsystem* Net = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UMRNetSubsystem>() : nullptr;
	const FMRServerEntry* Server = Net ? Net->GetServer() : nullptr;
	const FMRNetObject* Me = Net ? Net->FindObject(Net->GetPlayer().Id) : nullptr;
	if (!NetWorld || !NetWorld->IsActive() || !Server || Server->Social.IsEmpty() || !Me || Me->Name.IsEmpty())
	{
		UE_LOG(LogMeridian, Log, TEXT("MRAmbient: the arena: no relay for this server"));
		return;
	}
	const FString SocialServer = Server->SocialServer.IsEmpty() ? Server->Ws : Server->SocialServer;
	const FString For = SocialServer + TEXT("|") + Me->Name;
	if (!Social.IsValid() || SocialFor != For)
	{
		if (Social.IsValid())
		{
			Social->Close();
		}
		Social = MakeShared<FMRSocialClient>(Server->Social, SocialServer, Me->Name, Server->Origin);
		SocialFor = For;
		Social->Connect();
	}
	Arena = MakeUnique<FMRArenaPlayback>(World, Rid);
	TWeakObjectPtr<UMRAmbientSubsystem> Weak(this);
	Social->OnArena.BindLambda([Weak](TSharedPtr<FJsonObject> M)
	{
		if (UMRAmbientSubsystem* Self = Weak.Get(); Self && Self->Arena.IsValid())
		{
			Self->Arena->Receive(M);
		}
	});
	const UMRGameTimeSubsystem* Time = World->GetSubsystem<UMRGameTimeSubsystem>();
	Social->WatchArena(RoomFile, Time ? Time->GetGameHour() : -1.0);
	UE_LOG(LogMeridian, Log, TEXT("MRAmbient: watching the arena of %s on the relay %s"), *RoomFile, *Server->Social);
}

#include "Net/MRNetWorldSubsystem.h"

#include "Character/MRCharacter.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Character/MRSpriteData.h"
#include "Environment/MREnvironmentSubsystem.h"
#include "Audio/MRAudioSubsystem.h"
#include "Core/MRUnits.h"
#include "Dom/JsonObject.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/MRGameMode.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UnrealMeridian.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/MRNetLook.h"
#include "Net/MRNetObject.h"
#include "World/MRBgfSpriteComponent.h"
#include "Net/MRNetProjectile.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "World/MRRuntimeRooms.h"
#include "World/MRRuntimeRoom.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Net/MRAssetCache.h"
#include "World/MRBgf.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/MRProtocol.h"
#include "Player/MRPlayerState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/MRUISubsystem.h"
#include "Zones/MRZoneSubsystem.h"
#include "Character/MRCharacterMovementComponent.h"

namespace
{
	constexpr double MoveInterval = 0.25;      // the original client's MOVE_DELAY-ish update rate
	constexpr double OffRoomInterval = 1.0;    // move.c MOVE_OFF_ROOM_INTERVAL
	// faster than halfway between the walk and the run counts as running (BP_REQ_MOVE's speed)
	double RunCms() { return 0.5 * (UMRCharacterMovementComponent::WalkCms() + UMRCharacterMovementComponent::RunCms()); }
	constexpr int32 TurnThreshold = 128;       // 1/32 of a turn before a BP_REQ_TURN
	constexpr double SnapBackCm = MRUnits::CmPerSquare;  // the server moved us this far: follow it
	constexpr double AttackDelay = 0.25;       // gameuser.c ATTACK_DELAY
	constexpr double CloseDistanceCm = 5.0 * MRUnits::CmPerSquare;  // gameuser.h CLOSE_DISTANCE
	/** A room's weather sent this soon after entering it was already falling there. */
	constexpr double WeatherOnEntrySeconds = 3.0;

	int32 YawToKod(double Yaw)
	{
		const int32 A = FMath::RoundToInt(FRotator::ClampAxis(Yaw) * 4096.0 / 360.0);
		return A % 4096;
	}

	/** A look-dev bookmark (data/environment/lookdev_cameras.json) for the login backdrop. */
	bool FindBookmark(const FString& Name, FVector& OutLoc, FRotator& OutRot)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("environment"), TEXT("lookdev_cameras.json"));
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("cameras")))
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			if (C->GetStringField(TEXT("name")) != Name)
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>& L = C->GetArrayField(TEXT("location_cm"));
			const TArray<TSharedPtr<FJsonValue>>& R = C->GetArrayField(TEXT("rotation"));
			if (L.Num() == 3 && R.Num() >= 2)
			{
				OutLoc = FVector(L[0]->AsNumber(), L[1]->AsNumber(), L[2]->AsNumber());
				OutRot = FRotator(R[0]->AsNumber(), R[1]->AsNumber(), 0.0);
				return true;
			}
		}
		return false;
	}
}

bool UMRNetWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UMRNetWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UMRNetSubsystem* Net = GetNet();
	bActive = Net && UMRNetSubsystem::WantsOnline(&InWorld);
	if (!bActive)
	{
		return;
	}
	UE_LOG(LogMeridian, Log, TEXT("MRNet: this world plays online"));
	if (UMRZoneSubsystem* Zones = InWorld.GetSubsystem<UMRZoneSubsystem>())
	{
		Zones->SetServerDriven(true);
	}
	Net->OnPhaseChanged.AddUObject(this, &UMRNetWorldSubsystem::OnPhaseChanged);
	Net->OnRoomEntered.AddUObject(this, &UMRNetWorldSubsystem::OnRoomEntered);
	Net->OnObjectAdded.AddUObject(this, &UMRNetWorldSubsystem::OnObjectAdded);
	Net->OnObjectChanged.AddUObject(this, &UMRNetWorldSubsystem::OnObjectChanged);
	Net->OnObjectMoved.AddUObject(this, &UMRNetWorldSubsystem::OnObjectMoved);
	Net->OnObjectRemoved.AddUObject(this, &UMRNetWorldSubsystem::OnObjectRemoved);
	Net->OnEffect.AddUObject(this, &UMRNetWorldSubsystem::OnEffect);
	Net->OnProjectile.AddUObject(this, &UMRNetWorldSubsystem::OnProjectile);
	Net->OnSound.AddUObject(this, &UMRNetWorldSubsystem::OnSound);
	Net->OnLightChanged.AddUObject(this, &UMRNetWorldSubsystem::ApplyRoomLight);
	Net->OnRoomChange.AddUObject(this, &UMRNetWorldSubsystem::OnRoomChange);
	// the server says what sounds: the zones' own music, loops and random sounds step aside
	if (UMRAudioSubsystem* Audio = InWorld.GetSubsystem<UMRAudioSubsystem>())
	{
		Audio->SetServerDriven(true);
	}
	if (Net->GetPhase() == EMRNetPhase::InGame)
	{
		OnRoomEntered();  // a level reload while connected
	}
	else
	{
		ShowBackdrop();
	}
}

void UMRNetWorldSubsystem::Deinitialize()
{
	if (UMRNetSubsystem* Net = GetNet())
	{
		FOnMRNetEvent* Events[] = {&Net->OnPhaseChanged, &Net->OnRoomEntered, &Net->OnEffect, &Net->OnLightChanged};
		Net->OnProjectile.RemoveAll(this);
		Net->OnSound.RemoveAll(this);
		Net->OnRoomChange.RemoveAll(this);
		FOnMRNetObjectEvent* ObjectEvents[] = {&Net->OnObjectAdded, &Net->OnObjectChanged, &Net->OnObjectMoved, &Net->OnObjectRemoved};
		for (FOnMRNetEvent* E : Events)
		{
			E->RemoveAll(this);
		}
		for (FOnMRNetObjectEvent* E : ObjectEvents)
		{
			E->RemoveAll(this);
		}
	}
	Super::Deinitialize();
}

TStatId UMRNetWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRNetWorldSubsystem, STATGROUP_Tickables);
}

UMRNetSubsystem* UMRNetWorldSubsystem::GetNet() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
}

APlayerController* UMRNetWorldSubsystem::GetPC() const
{
	return GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
}

AMRNetObject* UMRNetWorldSubsystem::FindActor(uint32 Id) const
{
	const TWeakObjectPtr<AMRNetObject>* A = Actors.Find(Id);
	return A ? A->Get() : nullptr;
}

// ------------------------------------------------------------------------------ screens

void UMRNetWorldSubsystem::UpdateScreens()
{
	APlayerController* PC = GetPC();
	UMRNetSubsystem* Net = GetNet();
	UMRUISubsystem* UI = PC && PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
	if (!bActive || !UI || !Net)
	{
		return;
	}
	if (Net->GetPhase() == EMRNetPhase::InGame && PC->GetPawn())
	{
		UI->HideLogin();
		UI->ShowHUD(PC);
	}
	else
	{
		UI->RemoveHUD();
		UI->ShowLogin(PC);
	}
}

void UMRNetWorldSubsystem::ShowBackdrop()
{
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}
	FVector Loc;
	FRotator Rot;
	if (!FindBookmark(TEXT("square_overview"), Loc, Rot))
	{
		const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
		const FTransform T = Zones ? Zones->GetStartTransform(300) : FTransform::Identity;
		Loc = T.GetLocation() + FVector(0.0, 0.0, 400.0);
		Rot = T.Rotator();
	}
	PC->SetInitialLocationAndRotation(Loc, Rot);
	PC->SetViewTarget(PC);
}

void UMRNetWorldSubsystem::OnPhaseChanged()
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	if (Net->GetPhase() != EMRNetPhase::InGame)
	{
		// left the game (logoff or disconnect): back to the login screen over Raza
		ClearObjects();
		Rid = 0;
		bResting = false;
		++RoomTicket;  // a room still loading is no longer wanted
		LoadingRoom.Reset();
		if (APlayerController* PC = GetPC(); PC && PC->GetPawn())
		{
			APawn* Pawn = PC->GetPawn();
			PC->UnPossess();
			Pawn->Destroy();
			ShowBackdrop();
		}
	}
	UpdateScreens();
}

// ------------------------------------------------------------------------------ rooms and objects

void UMRNetWorldSubsystem::OnRoomEntered()
{
	UMRNetSubsystem* Net = GetNet();
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	if (!Net || !Zones)
	{
		return;
	}
	ClearObjects();
	RoomEnteredTime = FPlatformTime::Seconds();
	ApplyEffects(true);  // (the room's weather may have come just before it)
	const FMRNetPlayer& P = Net->GetPlayer();
	const int32 PrevRid = Rid;
	const int32 BuiltRid = Zones->RidForRoom(P.RoomFile);
	UE_LOG(LogMeridian, Log, TEXT("MRNet: entered %s (%s) -> zone %d, %d objects"), *P.RoomFile, *P.RoomName, BuiltRid, Net->GetObjects().Num());
	// the server names its room by the .roo's security value (clientd3d game.c): ours must be built from the same file
	const FMRZoneInfo* Zone = Zones->FindZone(BuiltRid);
	bRoomMatchesServer = !Zone || !Zone->bHasRooSecurity || MRNetRead::SecurityMatches(Zone->RooSecurity, P.RoomSecurity);
	if (BuiltRid && bRoomMatchesServer)
	{
		Rid = BuiltRid;
		FinishEnterRoom(PrevRid);
		return;
	}
	if (BuiltRid)
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: zone %d was built from a different %s than the server's (security %08x, server %08x); building the server's"),
			BuiltRid, *P.RoomFile, Zone->RooSecurity, P.RoomSecurity);
	}

	// a room we haven't built (or not this version of it): build it from the server's files (docs/adr/0012)
	UMRRuntimeRooms* Runtime = GetWorld()->GetSubsystem<UMRRuntimeRooms>();
	if (!Runtime)
	{
		Rid = 0;
		return;
	}
	Rid = 0;  // nothing moves or spawns until it's built
	LoadingRoom = P.RoomName;
	SetPawnFrozen(true);
	Net->SetStatus(FString::Printf(TEXT("Loading %s..."), *P.RoomName));
	const int32 Ticket = ++RoomTicket;
	TWeakObjectPtr<UMRNetWorldSubsystem> Weak(this);
	Runtime->Request(P.RoomFile, P.RoomName, P.RoomSecurity, [Weak, Ticket, PrevRid](int32 NewRid)
	{
		UMRNetWorldSubsystem* Self = Weak.Get();
		if (!Self || Ticket != Self->RoomTicket)
		{
			return;  // another room came since
		}
		Self->LoadingRoom.Reset();
		UMRNetSubsystem* N = Self->GetNet();
		if (!NewRid || !N || N->GetPhase() != EMRNetPhase::InGame)
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: couldn't build %s; staying put"), N ? *N->GetPlayer().RoomFile : TEXT("the room"));
			if (N)
			{
				N->SetStatus(FString::Printf(TEXT("%s couldn't be loaded."), *N->GetPlayer().RoomName));
			}
			Self->SetPawnFrozen(false);
			return;
		}
		Self->Rid = NewRid;
		Self->FinishEnterRoom(PrevRid);
	});
}

void UMRNetWorldSubsystem::SetPawnFrozen(bool bFrozen)
{
	APlayerController* PC = GetPC();
	ACharacter* Char = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	UCharacterMovementComponent* Move = Char ? Char->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	if (bFrozen)
	{
		Move->DisableMovement();
	}
	else if (Move->MovementMode == MOVE_None)
	{
		Move->SetMovementMode(MOVE_Walking);
	}
}

void UMRNetWorldSubsystem::FinishEnterRoom(int32 PrevRid)
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	Net->SetStatus(FString());
	ApplyRoomLight();
	const FMRWadingOverride Wading = FMRWadingOverride::FromServer(Net->GetPlayer().RoomFlags, Net->GetPlayer().Depth);
	if (AMRRuntimeRoom* Room = CurrentRuntimeRoom())
	{
		// the room as its file has it, then every change the server sent since BP_PLAYER (lifts at once)
		Room->SetWadingOverride(Wading);
		Room->ResetChanges();
		for (const FMRNetRoomChange& C : Net->GetRoomChanges())
		{
			ApplyRoomChange(Room, C);
		}
	}
	else if (Wading.Any())
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: the server overrides %s's wading depths; zone %d is built, so they aren't applied"),
			*Net->GetPlayer().RoomFile, Rid);
	}
	PlacePlayer(PrevRid == Rid);
	UpdateFrozen();
	ApplySelfLook();
	GatherProps();
	for (const TPair<uint32, FMRNetObject>& Pair : Net->GetObjects())
	{
		SpawnObject(Pair.Value);
	}
	LastSentKod = FIntPoint(-1, -1);
	LastSentAngle = -1;
	UpdateScreens();
	if (UMRRuntimeRooms* Runtime = GetWorld()->GetSubsystem<UMRRuntimeRooms>())
	{
		Runtime->Prefetch(Net->GetPlayer().RoomFile);  // the rooms one exit away build from the disk cache
	}
}

void UMRNetWorldSubsystem::ApplySelfLook()
{
	UMRNetSubsystem* Net = GetNet();
	APlayerController* PC = GetPC();
	const FMRNetObject* Self = Net ? Net->GetSelf() : nullptr;
	AMRCharacter* Pawn = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	FMRSpriteAppearance A;
	if (!Self || !Pawn || !MRNetLook::AppearanceFromObject(*Self, A))
	{
		return;
	}
	A.HeightPct = Pawn->GetSpriteAppearance().HeightPct;  // (not the server's: ours)
	if (A != Pawn->GetSpriteAppearance())
	{
		UE_LOG(LogMeridian, Log, TEXT("MRNet: our character looks %s"), *A.ToString());
		Pawn->SetSpriteAppearance(A);
	}
}

void UMRNetWorldSubsystem::PlacePlayer(bool bSameRoom)
{
	UMRNetSubsystem* Net = GetNet();
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	APlayerController* PC = GetPC();
	const FMRNetObject* Self = Net ? Net->GetSelf() : nullptr;
	if (!PC || !Zones || !Self)
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: the room has no player object"));
		return;
	}
	const FVector Floor = Zones->KodToWorld(Rid, Self->KodRow, Self->KodCol, true);
	const FRotator Facing(0.0, MRUnits::KodAngleToYaw(Self->Angle), 0.0);
	UE_LOG(LogMeridian, Verbose, TEXT("MRNet: placing the player at (%d, %d) of zone %d: %s"), Self->KodRow, Self->KodCol, Rid, *Floor.ToString());
	if (AMRPlayerState* PS = PC->GetPlayerState<AMRPlayerState>())
	{
		PS->SetZoneId(Rid);  // the minimap, music and moods follow the zone
	}
	if (APawn* Pawn = PC->GetPawn())
	{
		if (bSameRoom && FVector::Dist2D(Pawn->GetActorLocation(), Floor) <= SnapBackCm)
		{
			return;  // the same room again (its data was reloaded): we are where the server thinks
		}
		const FVector At = Floor + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0);
		Pawn->TeleportTo(At, Facing, false, true);
	}
	else if (AMRGameMode* GM = GetWorld()->GetAuthGameMode<AMRGameMode>())
	{
		GM->SpawnOnlinePlayer(PC, FTransform(Facing, Floor + FVector(0.0, 0.0, 100.0)));
	}
	PC->SetControlRotation(Facing);
}

FName UMRNetWorldSubsystem::LookFor(const FMRNetObject& Object) const
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	const FString Bgf = FPaths::GetBaseFilename(Object.Icon).ToLower();
	if (Object.IsPlayer())
	{
		// player_male / player_female, worn with the server's face parts and colours (MRNetLook)
		FMRSpriteAppearance A;
		MRNetLook::AppearanceFromObject(Object, A);
		return Lib.Looks.Contains(A.Look) ? A.Look : GetDefault<AMRCharacter>()->DefaultSpriteLook;
	}
	// by name first (NPCs share bodies), then a player's figure (soldiers, ghosts), then by the body bitmap
	for (const TPair<FName, FMRMonsterDef>& Pair : Lib.Monsters)
	{
		if (!Object.Name.IsEmpty() && Pair.Value.Name.Equals(Object.Name, ESearchCase::IgnoreCase))
		{
			return Pair.Value.Look;
		}
	}
	if (MRNetLook::IsPlayerFigure(Object))
	{
		FMRSpriteAppearance A;
		MRNetLook::AppearanceFromObject(Object, A);
		return Lib.Looks.Contains(A.Look) ? A.Look : GetDefault<AMRCharacter>()->DefaultSpriteLook;
	}
	if (LookByBgf.IsEmpty())
	{
		for (const TPair<FName, FMRMonsterDef>& Pair : Lib.Monsters)
		{
			const FMRSpriteLook* L = Lib.Looks.Find(Pair.Value.Look);
			const FMRSpritePart* Body = L ? L->Find(TEXT("body")) : nullptr;
			if (Body && !LookByBgf.Contains(Body->Bgf.ToLower()))
			{
				LookByBgf.Add(Body->Bgf.ToLower(), Pair.Value.Look);
			}
		}
	}
	const FName* Found = LookByBgf.Find(Bgf);
	return Found ? *Found : NAME_None;
}

void UMRNetWorldSubsystem::SpawnObject(const FMRNetObject& Object)
{
	UMRNetSubsystem* Net = GetNet();
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	if (!Net || !Zones || !Rid || Object.Id == Net->GetPlayer().Id || FindActor(Object.Id))
	{
		return;
	}
	// creatures we have a sprite for wear it; everything else is the server's bitmap, or a prop of
	// the world build standing on its square (a built zone's lamps, signs, tables)
	// (a logged-off player's ghost isn't a creature on the server, but carries a player's overlays)
	const FName Look = Object.IsCreature() || MRNetLook::IsPlayerFigure(Object) ? LookFor(Object) : NAME_None;
	if (!Look.IsNone() && !Object.IsPlayer() && MRNetLook::IsPlayerFigure(Object))
	{
		UE_LOG(LogMeridian, Log, TEXT("MRNet: %s drawn as a player figure (a player's overlays)"), *Object.Name);
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Floor = Zones->KodToWorld(Rid, Object.KodRow, Object.KodCol, true);
	AMRNetObject* Actor = GetWorld()->SpawnActor<AMRNetObject>(AMRNetObject::StaticClass(), FTransform(Floor), Params);
	if (!Actor)
	{
		return;
	}
	Actor->Init(Object.Id, Look, Object.Name);
	Actor->SetServerInfo(Object.Flags, Object.NameColor, Object.MinimapFlags);
	if (!Object.IsCreature())
	{
		Actor->SetStatic();
	}
	FMRSpriteAppearance A;
	double PropTop = 0.0;
	if (!Look.IsNone())
	{
		if (MRNetLook::AppearanceFromObject(Object, A))
		{
			Actor->SetAppearance(A);
		}
	}
	else if (!Object.IsCreature() && FindPropAt(Floor, PropTop))
	{
		Actor->SetShownByProp(PropTop);
		UE_LOG(LogMeridian, Log, TEXT("MRNet: %s drawn by the prop on its square"), *Object.Name);
		if (Object.OverlayParts.Num() > 0)
		{
			// its overlays still show, hung from its (unseen) bitmap's hotspots: the flagpole's flag
			AttachBgfSprite(Actor, Object, true);
		}
	}
	else
	{
		AttachBgfSprite(Actor, Object);  // no sprite of ours: the server's own bitmap
	}
	Actor->SetDrawEffect(Object.DrawEffect);
	Actor->Place(Floor, Object.Angle);
	Actors.Add(Object.Id, Actor);
}

void UMRNetWorldSubsystem::GatherProps()
{
	Props.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		// visible props of the world build (not its invisible blockers)
		if (It->ActorHasTag(TEXT("ZoneProp")) && !It->ActorHasTag(TEXT("ZoneBlocker")))
		{
			Props.Add(*It);
		}
	}
}

bool UMRNetWorldSubsystem::FindPropAt(const FVector& Floor, double& OutTop) const
{
	// on the same square as the object: the world build put it at the object's Kod position
	constexpr double Near = MRUnits::CmPerSquare * 0.3;
	for (const TWeakObjectPtr<AActor>& P : Props)
	{
		const AActor* A = P.Get();
		if (A && FVector::Dist2D(A->GetActorLocation(), Floor) < Near)
		{
			FVector Origin, Extent;
			A->GetActorBounds(true, Origin, Extent);
			OutTop = Origin.Z + Extent.Z;
			return true;
		}
	}
	return false;
}

FString UMRNetWorldSubsystem::OverlayKeyOf(const FMRNetObject& Object)
{
	FString Key;
	for (const FMRNetOverlay& V : Object.OverlayParts)
	{
		Key += FString::Printf(TEXT("%s@%d;"), *V.Bgf, V.Hotspot);
	}
	return Key;
}

void UMRNetWorldSubsystem::AttachBgfOverlays(AMRNetObject* Actor, const FMRNetObject& Object)
{
	// every overlay's bitmap first, then all at once (they arrive in any order)
	Actor->SetOverlayKey(OverlayKeyOf(Object));
	if (Object.OverlayParts.Num() == 0)
	{
		return;
	}
	struct FPending
	{
		TArray<UMRBgfSpriteComponent::FOverlay> Overlays;
		int32 Left = 0;
	};
	TSharedRef<FPending> P = MakeShared<FPending>();
	P->Overlays.SetNum(Object.OverlayParts.Num());
	P->Left = Object.OverlayParts.Num();
	TWeakObjectPtr<AMRNetObject> WeakActor(Actor);
	for (int32 i = 0; i < Object.OverlayParts.Num(); ++i)
	{
		const FMRNetOverlay& V = Object.OverlayParts[i];
		P->Overlays[i].Hotspot = V.Hotspot;
		P->Overlays[i].Animation = V.Animation;
		FetchBgf(V.Bgf + TEXT(".bgf"), [WeakActor, P, i](TSharedPtr<const FMRBgf> Bgf)
		{
			P->Overlays[i].Bgf = Bgf;
			if (--P->Left == 0)
			{
				AMRNetObject* A = WeakActor.Get();
				if (A && A->GetBgfSprite())
				{
					A->GetBgfSprite()->SetOverlays(P->Overlays);
				}
			}
		});
	}
}

void UMRNetWorldSubsystem::AttachBgfSprite(AMRNetObject* Actor, const FMRNetObject& Object, bool bBaseHidden)
{
	UMRNetSubsystem* Net = GetNet();
	FMRAssetCache* Cache = Net ? Net->GetAssets() : nullptr;
	const FString File = Object.Icon.ToLower();
	if (!Cache || File.IsEmpty() || !Cache->IsListed(File))
	{
		UE_LOG(LogMeridian, Log, TEXT("MRNet: no sprite or bitmap for %s (%s); not drawn"), *Object.Name, *Object.Icon);
		return;
	}
	const FMRNetAnimation Standing = Object.Animation;
	const FMRNetAnimation Moving = Object.MotionAnimation;
	TWeakObjectPtr<AMRNetObject> WeakActor(Actor);
	TWeakObjectPtr<UMRNetWorldSubsystem> WeakThis(this);
	const FMRNetObject Copy = Object;
	FetchBgf(File, [WeakActor, WeakThis, File, Standing, Moving, bBaseHidden, Copy](TSharedPtr<const FMRBgf> Bgf)
	{
		if (AMRNetObject* A = WeakActor.Get())
		{
			A->SetBgfSprite(File, Bgf);
			A->SetServerAnimation(Standing, Moving);
			if (A->GetBgfSprite())
			{
				A->GetBgfSprite()->SetBaseHidden(bBaseHidden);
			}
			if (UMRNetWorldSubsystem* Self = WeakThis.Get())
			{
				Self->AttachBgfOverlays(A, Copy);
			}
		}
	});
}

void UMRNetWorldSubsystem::FetchBgf(const FString& InFile, TFunction<void(TSharedPtr<const FMRBgf>)> Done)
{
	const FString File = InFile.ToLower();
	if (const TSharedPtr<const FMRBgf>* Known = Bgfs.Find(File))
	{
		Done(*Known);
		return;
	}
	UMRNetSubsystem* Net = GetNet();
	FMRAssetCache* Cache = Net ? Net->GetAssets() : nullptr;
	if (!Cache || File.IsEmpty() || !Cache->IsListed(File))
	{
		return;
	}
	TWeakObjectPtr<UMRNetWorldSubsystem> Weak(this);
	Cache->Fetch(File, [Weak, File, Done = MoveTemp(Done)](bool bOk, const TArray<uint8>& Bytes)
	{
		UMRNetWorldSubsystem* Self = Weak.Get();
		if (!Self)
		{
			return;
		}
		if (const TSharedPtr<const FMRBgf>* Known = Self->Bgfs.Find(File))
		{
			Done(*Known);  // (another request parsed it meanwhile)
			return;
		}
		TSharedPtr<FMRBgf> Bgf = MakeShared<FMRBgf>();
		FString Error;
		if (!bOk || !Bgf->Load(Bytes, Error))
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRNet: %s: %s"), *File, bOk ? *Error : TEXT("not downloaded"));
			return;
		}
		Self->Bgfs.Add(File, Bgf);
		Done(Bgf);
	});
}

void UMRNetWorldSubsystem::ClearObjects()
{
	CancelChoosing();  // (a new room, or its ids renumbered)
	for (const TWeakObjectPtr<AActor>& P : Projectiles)
	{
		if (AActor* A = P.Get())
		{
			A->Destroy();
		}
	}
	Projectiles.Reset();
	LastAttackedId = 0;
	ClearTarget();  // a new room (or the same one reloaded: its ids may have changed)
	AimId = 0;
	AimStack.Reset();
	for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : Actors)
	{
		if (AMRNetObject* A = Pair.Value.Get())
		{
			A->Destroy();
		}
	}
	Actors.Reset();
}

void UMRNetWorldSubsystem::OnObjectAdded(uint32 Id)
{
	if (const FMRNetObject* O = GetNet() ? GetNet()->FindObject(Id) : nullptr)
	{
		SpawnObject(*O);
	}
}

void UMRNetWorldSubsystem::OnObjectChanged(uint32 Id)
{
	// a new look (e.g. a corpse) or a player who changed equipment: draw it again
	if (GetNet() && Id == GetNet()->GetPlayer().Id)
	{
		ApplySelfLook();
		if (const FMRNetObject* Self = GetNet()->GetSelf())
		{
			PlayServerAction(*Self);  // our own swing, as the server saw it (first person: BP_PLAYER_OVERLAY)
		}
		return;
	}
	AMRNetObject* A = FindActor(Id);
	const FMRNetObject* O = GetNet() ? GetNet()->FindObject(Id) : nullptr;
	if (!A || !O)
	{
		return;
	}
	// another body (a corpse), a creature that stopped being one, or another bitmap: draw it again
	const FName Want = O->IsCreature() || MRNetLook::IsPlayerFigure(*O) ? LookFor(*O) : NAME_None;
	const bool bRespawn = Want != A->GetLook() || A->IsStatic() == O->IsCreature()
		|| (Want.IsNone() && !A->IsShownByProp() && A->GetBgfName() != O->Icon.ToLower())
		|| (Want.IsNone() && OverlayKeyOf(*O) != A->GetOverlayKey());  // a flag raised, lowered or changed
	if (bRespawn)
	{
		A->Destroy();
		Actors.Remove(Id);
		SpawnObject(*O);
		return;
	}
	A->SetServerInfo(O->Flags, O->NameColor, O->MinimapFlags);
	A->SetDrawEffect(O->DrawEffect);
	FMRSpriteAppearance Look;
	if (Want.IsNone())
	{
		A->SetServerAnimation(O->Animation, O->MotionAnimation);
	}
	else if (MRNetLook::AppearanceFromObject(*O, Look))
	{
		A->SetAppearance(Look);  // the same body: new face parts or colours
	}
	if (!Want.IsNone())
	{
		PlayServerAction(*O);
	}
}

void UMRNetWorldSubsystem::PlayServerAction(const FMRNetObject& O)
{
	// one-offs: Kod sets them for a single SomethingChanged (player.kod DoAttackSwing, DoFistAttack,
	// DoCast; monster.kod's attack), then the object is back to its usual animation
	const FMRNetAnimation* Body = O.Animation.Type == MRMsg::ANIMATE_ONCE ? &O.Animation : nullptr;
	const FMRNetOverlay* Arm = O.OverlayParts.FindByPredicate([](const FMRNetOverlay& Ov) { return Ov.Animation.Type == MRMsg::ANIMATE_ONCE; });
	if (!Body && !Arm)
	{
		return;
	}
	UMRSpriteBodyComponent* Sprite = nullptr;
	FName Look;
	const APlayerController* PC = GetPC();
	if (const AMRCharacter* Pawn = PC && GetNet() && O.Id == GetNet()->GetPlayer().Id ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr)
	{
		Sprite = Pawn->GetSpriteBody();
		Look = Pawn->GetSpriteAppearance().Look;
	}
	else if (const AMRNetObject* A = FindActor(O.Id))
	{
		Sprite = A->GetSpriteBody();
		Look = A->GetLook();
	}
	if (!Sprite)
	{
		return;  // drawn from its own bitmap: its animation record plays it (SetServerAnimation)
	}
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	FName Action;
	if (const TMap<FName, FMRSpriteAction>* Own = Lib.LookActions.Find(Look))
	{
		// a monster: its attack is its only one-off (PANM_MONSTER_ATTACK)
		if (Body && Own->Contains(TEXT("attack")))
		{
			Action = TEXT("attack");
		}
	}
	else
	{
		// a player: the action with the server's groups (player.kod SendAnimation: the body's for a
		// swing, an arm's for a cast, a point or a wave)
		const auto Same = [](const FMRSpriteTrackDef* T, const FMRNetAnimation& A)
		{
			return T && T->Mode == FMRSpriteTrackDef::EMode::Once && T->Low == A.GroupLow && T->High == A.GroupHigh;
		};
		for (const TPair<FName, FMRSpriteAction>& P : Lib.Actions)
		{
			const FMRSpriteTrackDef* BodyTrack = P.Value.Tracks.Find(TEXT("body"));
			if (Body ? Same(BodyTrack, *Body)
				: !BodyTrack && (Same(P.Value.Tracks.Find(TEXT("right_arm")), Arm->Animation) || Same(P.Value.Tracks.Find(TEXT("left_arm")), Arm->Animation)))
			{
				Action = P.Key;
				break;
			}
		}
	}
	if (!Action.IsNone())
	{
		UE_LOG(LogMeridian, Verbose, TEXT("MRNet: %s plays %s"), *O.Name, *Action.ToString());
		Sprite->PlayAction(Action);
	}
}

void UMRNetWorldSubsystem::OnObjectRemoved(uint32 Id)
{
	if (Id == TargetId)
	{
		ClearTarget();  // gone (dead, picked up, left the room)
	}
	if (AMRNetObject* A = FindActor(Id))
	{
		A->Destroy();
	}
	Actors.Remove(Id);
}

void UMRNetWorldSubsystem::OnObjectMoved(uint32 Id)
{
	UMRNetSubsystem* Net = GetNet();
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	const FMRNetObject* O = Net ? Net->FindObject(Id) : nullptr;
	if (!O || !Zones || !Rid)
	{
		return;
	}
	const FVector Floor = Zones->KodToWorld(Rid, O->KodRow, O->KodCol, true);
	if (Id == Net->GetPlayer().Id)
	{
		// the server put us somewhere else (off the map, blocked, a spell): go there
		APawn* Pawn = GetPC() ? GetPC()->GetPawn() : nullptr;
		if (Pawn && FVector::Dist2D(Pawn->GetActorLocation(), Floor) > SnapBackCm)
		{
			UE_LOG(LogMeridian, Log, TEXT("MRNet: server moved the player to (%d, %d)"), O->KodRow, O->KodCol);
			Pawn->TeleportTo(Floor + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0), Pawn->GetActorRotation(), false, true);
			LastSentKod = FIntPoint(O->KodRow, O->KodCol);
		}
		return;
	}
	if (AMRNetObject* A = FindActor(Id))
	{
		A->MoveTo(Floor, O->Speed);
		A->TurnTo(O->Angle);
	}
}

// ------------------------------------------------------------------------------ targets

double UMRNetWorldSubsystem::NameDistanceCm()
{
	return 15.0 * MRUnits::CmPerSquare;
}

void UMRNetWorldSubsystem::SetTarget(uint32 Id)
{
	if (Id != TargetId)
	{
		TargetId = Id;
		UE_LOG(LogMeridian, Verbose, TEXT("MRNet: target %u"), Id);
		OnTargetChanged.Broadcast();
	}
}

void UMRNetWorldSubsystem::TargetSelf()
{
	if (const UMRNetSubsystem* Net = GetNet())
	{
		SetTarget(Net->GetPlayer().Id);
	}
}

void UMRNetWorldSubsystem::TargetAim()
{
	SetTarget(AimId);
}

bool UMRNetWorldSubsystem::IsInSight(const AMRNetObject* A, FVector2D& OutScreen, double& OutDistance, double MaxDistance) const
{
	const APlayerController* PC = GetPC();
	if (!A || !PC || !PC->PlayerCameraManager || A->GetDrawEffect() == MRMsg::DRAWFX_INVISIBLE)
	{
		return false;
	}
	const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Feet = A->GetActorLocation() - FVector(0.0, 0.0, 80.0);
	const FVector Middle = (Feet + A->GetNameAnchor()) * 0.5;
	OutDistance = FVector::Dist(Camera, Middle);
	if (MaxDistance >= 0.0 && OutDistance > MaxDistance)
	{
		return false;  // too far to matter: no trace (the aim asks every object, every frame)
	}
	if (!PC->ProjectWorldLocationToScreen(Middle, OutScreen, true))
	{
		return false;
	}
	int32 W = 0, H = 0;
	PC->GetViewportSize(W, H);
	if (OutScreen.X < 0 || OutScreen.Y < 0 || OutScreen.X > W || OutScreen.Y > H)
	{
		return false;
	}
	// walls hide it (not other objects or the pawn)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRNetSight), false, PC->GetPawn());
	Params.AddIgnoredActor(A);
	FHitResult Hit;
	for (int32 Tries = 0; Tries < 3; ++Tries)
	{
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Camera, Middle, ECC_WorldStatic, Params))
		{
			return true;
		}
		const AActor* By = Hit.GetActor();
		if (By && By->IsA<AMRNetObject>())
		{
			return true;
		}
		// an object drawn by a prop of the world build (a table, a lamp, a sign): the line ends
		// inside that prop, or its post's blocker; it doesn't hide its own object (2026-10-10:
		// such objects could never be aimed at, so never looked at)
		if (By && A->IsShownByProp() && By->ActorHasTag(TEXT("ZoneProp"))
			&& FVector::Dist2D(By->GetActorLocation(), A->GetActorLocation()) < MRUnits::CmPerSquare * 0.75)
		{
			Params.AddIgnoredActor(By);
			continue;
		}
		return false;
	}
	return false;
}

void UMRNetWorldSubsystem::UpdateAim()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(MRNetAim);
	AimId = 0;
	AimStack.Reset();
	const APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}
	int32 W = 0, H = 0;
	PC->GetViewportSize(W, H);
	// what's under the free cursor (UMRUISubsystem::UsesFreeCursor), else the middle of the view
	FVector2D Centre(W * 0.5, H * 0.5);
	float MouseX = 0.f, MouseY = 0.f;
	if (PC->bShowMouseCursor && UMRUISubsystem::UsesFreeCursor() && PC->GetMousePosition(MouseX, MouseY))
	{
		Centre = FVector2D(MouseX, MouseY);
	}
	TArray<TPair<double, uint32>> Hits;
	for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : Actors)
	{
		const AMRNetObject* A = Pair.Value.Get();
		FVector2D Screen;
		double Dist = 0.0;
		if (!A || !IsInSight(A, Screen, Dist, NameDistanceCm()))
		{
			continue;
		}
		// on it: within its drawn size on screen (half its height, at least a few pixels)
		FVector2D Top;
		if (!PC->ProjectWorldLocationToScreen(A->GetNameAnchor(), Top, true))
		{
			continue;
		}
		const double Radius = FMath::Max(12.0, FMath::Abs(Screen.Y - Top.Y));
		const double Off = FVector2D::Distance(Screen, Centre);
		if (Off <= Radius)
		{
			Hits.Add({Off / Radius + Dist / NameDistanceCm() * 0.1, Pair.Key});
		}
	}
	Hits.Sort([](const TPair<double, uint32>& A, const TPair<double, uint32>& B) { return A.Key < B.Key; });
	for (const TPair<double, uint32>& Hit : Hits)
	{
		AimStack.Add(Hit.Value);
	}
	AimId = AimStack.Num() > 0 ? AimStack[0] : 0;
}

void UMRNetWorldSubsystem::TargetNextOrPrevious(bool bNext)
{
	// the attackable objects in view, left to right (the original: GetObjects3D(OF_ATTACKABLE), not invisible)
	TArray<TPair<double, uint32>> InView;
	for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : Actors)
	{
		const AMRNetObject* A = Pair.Value.Get();
		FVector2D Screen;
		double Dist = 0.0;
		if (A && (A->GetFlags() & MRMsg::OF_ATTACKABLE) && IsInSight(A, Screen, Dist))
		{
			InView.Add({Screen.X, Pair.Key});
		}
	}
	if (InView.IsEmpty())
	{
		ClearTarget();
		return;
	}
	InView.Sort([](const TPair<double, uint32>& A, const TPair<double, uint32>& B) { return A.Key < B.Key; });
	const int32 Current = InView.IndexOfByPredicate([this](const TPair<double, uint32>& E) { return E.Value == TargetId; });
	const int32 N = InView.Num();
	const int32 Next = Current == INDEX_NONE ? (bNext ? 0 : N - 1) : (Current + (bNext ? 1 : N - 1)) % N;
	SetTarget(InView[Next].Value);
}

TArray<uint32> UMRNetWorldSubsystem::GettableAtAim() const
{
	TArray<uint32> Out;
	if (const UMRNetSubsystem* Net = GetNet())
	{
		for (const uint32 Id : AimStack)
		{
			const FMRNetObject* O = Net->FindObject(Id);
			if (O && (O->Flags & MRMsg::OF_GETTABLE))
			{
				Out.Add(Id);
			}
		}
	}
	return Out;
}

bool UMRNetWorldSubsystem::UseAim()
{
	UMRNetSubsystem* Net = GetNet();
	const uint32 Id = TargetId && TargetId != (Net ? Net->GetPlayer().Id : 0) ? TargetId : AimId;
	const FMRNetObject* O = Net && Id ? Net->FindObject(Id) : nullptr;
	if (!O)
	{
		return false;
	}
	if (O->Flags & MRMsg::OF_CONTAINER)
	{
		Net->RequestContents(Id);
		return true;
	}
	if (O->Flags & MRMsg::OF_ACTIVATABLE)
	{
		Net->Activate(Id);
		return true;
	}
	return false;
}

bool UMRNetWorldSubsystem::LookAtTarget(bool bAimFirst)
{
	UMRNetSubsystem* Net = GetNet();
	// (a right click with the free cursor looks at what it's on, as the original's right click)
	const uint32 Id = bAimFirst && AimId ? AimId : TargetId ? TargetId : AimId;
	if (!Net || !Id)
	{
		return false;
	}
	Net->RequestLook(Id);
	return true;
}

// ------------------------------------------------------------------------------ movement up

void UMRNetWorldSubsystem::Tick(float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(MRNetWorldTick);
	Super::Tick(DeltaTime);
	UMRNetSubsystem* Net = GetNet();
	// (not while the server saves or our data is being reloaded: the room's object id may change)
	if (Net && Net->GetPhase() == EMRNetPhase::InGame && Rid && !Net->IsWaiting())
	{
		SendMovement(FPlatformTime::Seconds());
	}
	if (Net && Net->GetPhase() == EMRNetPhase::InGame && Rid)
	{
		UpdateAim();
	}
}

void UMRNetWorldSubsystem::RequestGo()
{
	UMRNetSubsystem* Net = GetNet();
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	APlayerController* PC = GetPC();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Net || Net->GetPhase() != EMRNetPhase::InGame || !Rid || !Zones || !Pawn || Net->IsWaiting() || bResting)
	{
		return;
	}
	// the server checks the square it last heard of: send where we are first (the move order is kept)
	SendPositionNow();
	const FIntPoint Kod = Zones->WorldToKod(Rid, Pawn->GetActorLocation());
	UE_LOG(LogMeridian, Log, TEXT("MRNet: go at (%d, %d) of zone %d: BP_REQ_GO"), Kod.X / MRMsg::KodFineness, Kod.Y / MRMsg::KodFineness, Rid);
	Net->RequestGo();
}

void UMRNetWorldSubsystem::SendPositionNow()
{
	UMRNetSubsystem* Net = GetNet();
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	APlayerController* PC = GetPC();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Net || !Zones || !Pawn || !Rid)
	{
		return;
	}
	const FIntPoint Kod = Zones->WorldToKod(Rid, Pawn->GetActorLocation());
	if (Kod != LastSentKod)
	{
		const bool bRunning = Pawn->GetVelocity().Size2D() > RunCms();
		Net->RequestMove(Kod.X, Kod.Y, bRunning ? MRMsg::SPEED_RUN : MRMsg::SPEED_WALK);
		LastSentKod = Kod;
		LastMoveTime = FPlatformTime::Seconds();
	}
}

// ------------------------------------------------------------------------------ combat

uint32 UMRNetWorldSubsystem::Attack()
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net || Net->GetPhase() != EMRNetPhase::InGame || !Rid || Net->IsWaiting())
	{
		return 0;
	}
	if (IsChoosingTarget())
	{
		ChooseTarget();  // the click picks the waiting spell's or item's target instead
		return 0;
	}
	if (bResting || Net->GetEffects().bParalyzed)
	{
		return 0;  // mermain.c InterfaceAction: no attacks while resting or paralyzed
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastAttackTime < AttackDelay)
	{
		return 0;
	}
	LastAttackTime = Now;
	const uint32 SelfId = Net->GetPlayer().Id;
	uint32 Id = 0;
	FVector2D Screen;
	double Dist = 0.0;
	if (TargetId)
	{
		// the chosen target, if we can see it (ourselves too: the server answers that one)
		const AMRNetObject* A = FindActor(TargetId);
		if (TargetId != SelfId && !(A && IsInSight(A, Screen, Dist)))
		{
			Net->AddGameMessage(TEXT("You can't see your selected target."));  // IDS_TARGETNOTVISIBLEFORATTACK
			return 0;
		}
		Id = TargetId;
	}
	else if (const AMRNetObject* A = FindActor(AimId); A && (A->GetFlags() & MRMsg::OF_ATTACKABLE))
	{
		Id = AimId;  // what the crosshair is on
	}
	else
	{
		// the nearest attackable thing in view, close by
		double Best = CloseDistanceCm;
		for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : Actors)
		{
			const AMRNetObject* O = Pair.Value.Get();
			if (O && (O->GetFlags() & MRMsg::OF_ATTACKABLE) && IsInSight(O, Screen, Dist) && Dist <= Best)
			{
				Best = Dist;
				Id = Pair.Key;
			}
		}
	}
	if (!Id)
	{
		return 0;
	}
	SendPositionNow();  // gameuser.c MoveUpdatePosition: the range is checked from here
	Net->Attack(Id);
	LastAttackedId = Id;
	const FMRNetObject* O = Net->FindObject(Id);
	UE_LOG(LogMeridian, Log, TEXT("MRNet: attack %u (%s)"), Id, O ? *O->Name : TEXT("?"));
	return Id;
}

bool UMRNetWorldSubsystem::CastSpell(uint32 SpellId)
{
	UMRNetSubsystem* Net = GetNet();
	const FMRNetSpell* Spell = Net ? Net->GetNetWorld().FindSpell(SpellId) : nullptr;
	if (!Spell || Net->GetPhase() != EMRNetPhase::InGame || Net->IsWaiting())
	{
		return false;
	}
	PendingItem = 0;
	PendingSpell = 0;
	if (Net->GetEffects().bParalyzed)
	{
		Net->AddGameMessage(TEXT("You can't lift your hands to cast the spell!"));  // IDS_SPELLPARALYZED
		return false;
	}
	if (bResting)
	{
		Net->AddGameMessage(TEXT("You can't cast spells while you're resting."));  // IDS_SPELLRESTING
		return false;
	}
	if (Spell->Targets == 0)
	{
		Net->CastSpell(SpellId, {});
		return true;
	}
	const uint32 SelfId = Net->GetPlayer().Id;
	if (TargetId)
	{
		// the chosen target if we can see it (merintr IDS_TARGETNOTVISIBLEFORCAST)
		FVector2D Screen;
		double Dist = 0.0;
		const AMRNetObject* A = FindActor(TargetId);
		if (TargetId != SelfId && !(A && IsInSight(A, Screen, Dist)))
		{
			Net->AddGameMessage(TEXT("You can't see your selected target."));
			return false;
		}
		Net->CastSpell(SpellId, {TargetId});
		return true;
	}
	if (AimId)
	{
		Net->CastSpell(SpellId, {AimId});
		return true;
	}
	PendingSpell = SpellId;  // the next choice picks it (merintr GAME_SELECT)
	return false;
}

void UMRNetWorldSubsystem::ApplyItem(uint32 ItemId)
{
	PendingSpell = 0;
	PendingItem = ItemId;
}

FString UMRNetWorldSubsystem::GetChoosingText() const
{
	const UMRNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return FString();
	}
	if (const FMRNetSpell* S = PendingSpell ? Net->GetNetWorld().FindSpell(PendingSpell) : nullptr)
	{
		return FString::Printf(TEXT("Cast %s on what?"), *S->Object.Name);
	}
	if (const FMRNetObject* O = PendingItem ? Net->FindInventory(PendingItem) : nullptr)
	{
		return FString::Printf(TEXT("Use %s on what?"), *O->Name);
	}
	return FString();
}

bool UMRNetWorldSubsystem::ChooseTarget(uint32 Id)
{
	UMRNetSubsystem* Net = GetNet();
	Id = Id ? Id : AimId;
	if (!Net || !IsChoosingTarget() || !Id)
	{
		return false;
	}
	if (PendingSpell)
	{
		Net->CastSpell(PendingSpell, {Id});
	}
	else
	{
		Net->Apply(PendingItem, Id);
	}
	PendingSpell = 0;
	PendingItem = 0;
	return true;
}

void UMRNetWorldSubsystem::SetResting(bool bRest)
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net || Net->GetPhase() != EMRNetPhase::InGame)
	{
		return;
	}
	if (bRest == bResting)
	{
		Net->AddGameMessage(bRest ? TEXT("You're already resting.") : TEXT("You're not resting."));  // IDS_RESTING, IDS_STANDING
		return;
	}
	bResting = bRest;
	if (bRest)
	{
		Net->Rest();
	}
	else
	{
		Net->Stand();
	}
	Net->AddGameMessage(bRest ? TEXT("You rest.") : TEXT("You stop resting."));  // IDS_REST, IDS_STAND
	UpdateFrozen();
}

void UMRNetWorldSubsystem::UpdateFrozen()
{
	const UMRNetSubsystem* Net = GetNet();
	if (Rid && LoadingRoom.IsEmpty())
	{
		SetPawnFrozen(bResting || (Net && Net->GetEffects().bParalyzed));
	}
}

void UMRNetWorldSubsystem::CancelChoosing()
{
	PendingSpell = 0;
	PendingItem = 0;
}

bool UMRNetWorldSubsystem::FeetOf(uint32 Id, FVector& Out) const
{
	const UMRNetSubsystem* Net = GetNet();
	const APlayerController* PC = GetPC();
	if (const APawn* Pawn = PC && Net && Id == Net->GetPlayer().Id ? PC->GetPawn() : nullptr)
	{
		Out = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
		return true;
	}
	if (const AMRNetObject* A = FindActor(Id))
	{
		Out = A->GetActorLocation() - FVector(0.0, 0.0, A->GetSimpleCollisionHalfHeight());
		return true;
	}
	return false;
}

void UMRNetWorldSubsystem::OnProjectile(const FMRNetProjectile& P)
{
	FVector From;
	if (!Rid || !FeetOf(P.Source, From))
	{
		return;  // project.c: both ends must be in the room
	}
	// to the target; a radius shot sends Number of them out to Range evenly round (Range * 1000 of
	// the original's 1024 fine units a square)
	TArray<FVector> Ends;
	if (P.bRadius)
	{
		const double Reach = P.Range * 1000.0 / 1024.0 * MRUnits::CmPerSquare;
		for (int32 i = 0; i < P.Number; ++i)
		{
			const double Angle = UE_TWO_PI * i / FMath::Max<int32>(1, P.Number);
			Ends.Add(From + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Reach);
		}
	}
	else
	{
		FVector To;
		if (P.Dest == P.Source || !FeetOf(P.Dest, To))
		{
			return;
		}
		Ends.Add(To);
	}
	const double SpeedCms = P.Speed * MRUnits::CmPerSquare;  // Speed squares a second
	TArray<TWeakObjectPtr<AMRNetProjectile>> Made;
	for (const FVector& To : Ends)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMRNetProjectile* Shot = GetWorld()->SpawnActor<AMRNetProjectile>(From, FRotator::ZeroRotator, Params);
		if (!Shot)
		{
			continue;
		}
		Shot->Launch(From, To, SpeedCms, (P.Flags & MRMsg::PROJ_FLAG_FOLLOWGROUND) != 0);
		Shot->SetLight(P.Light);
		Projectiles.Add(Shot);
		Made.Add(Shot);
	}
	Projectiles.RemoveAll([](const TWeakObjectPtr<AActor>& A) { return !A.IsValid(); });
	UE_LOG(LogMeridian, Log, TEXT("MRNet: %s shot from %u (%d)"), *P.Icon, P.Source, Made.Num());
	const FMRNetAnimation Animation = P.Animation;
	const int32 Effect = P.Effect;
	FetchBgf(P.Icon, [Made, Animation, Effect](TSharedPtr<const FMRBgf> Bgf)
	{
		for (const TWeakObjectPtr<AMRNetProjectile>& Shot : Made)
		{
			if (AMRNetProjectile* S = Shot.Get())
			{
				S->SetBgf(Bgf, Animation, Effect);
			}
		}
	});
}

void UMRNetWorldSubsystem::OnSound(const FMRNetSound& S)
{
	UMRAudioSubsystem* Audio = GetWorld()->GetSubsystem<UMRAudioSubsystem>();
	if (!Audio)
	{
		return;
	}
	switch (S.Kind)
	{
	case FMRNetSound::EKind::Music:
		Audio->ServerMusic(S.File);
		return;
	case FMRNetSound::EKind::Stop:
		Audio->ServerStopSound(S.File, S.ObjectId);
		return;
	case FMRNetSound::EKind::StopLoops:
		Audio->ServerStopLoops();
		return;
	default:
		break;
	}
	// game.c GamePlaySound: at its object, else at the square (its middle), else 2D at the player
	FVector At = FVector::ZeroVector;
	bool b2D = true;
	constexpr double EarCm = 120.0;  // (an object's or a square's sound, about head high)
	if (S.ObjectId && FeetOf(S.ObjectId, At))
	{
		At.Z += EarCm;
		b2D = false;
	}
	else if ((S.Row > 0 || S.Col > 0) && Rid)
	{
		if (const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>())
		{
			At = Zones->GridToWorld(Rid, S.Row, S.Col, true) + FVector(0.0, 0.0, EarCm);
			b2D = false;
		}
	}
	// SF_RANDOM_PITCH: the original plays it as it is
	Audio->ServerSound(S.File, At, b2D, (S.Flags & MRMsg::SF_LOOP) != 0, 1.f, S.ObjectId);
}

void UMRNetWorldSubsystem::ApplyRoomLight()
{
	UMRNetSubsystem* Net = GetNet();
	static const TCHAR* CollectionPath = TEXT("/Game/Generated/Environment/Materials/MPC_Environment.MPC_Environment");
	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, CollectionPath);
	if (!Net || !Collection)
	{
		return;
	}
	// d3drender.c: a sector's light over 127 adds the room's ambient light; the player's own light falls off with distance
	const FMRNetPlayer& P = Net->GetPlayer();
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), Collection, TEXT("RoomAmbient"), P.AmbientLight / 255.f);
	UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), Collection, TEXT("PlayerLight"), P.PlayerLight / 255.f);
}

AMRRuntimeRoom* UMRNetWorldSubsystem::CurrentRuntimeRoom() const
{
	const UMRNetSubsystem* Net = GetNet();
	const UMRRuntimeRooms* Runtime = GetWorld()->GetSubsystem<UMRRuntimeRooms>();
	if (!Net || !Runtime || Rid < UMRRuntimeRooms::RuntimeRidBase || Runtime->FindBuiltRid(Net->GetPlayer().RoomFile) != Rid)
	{
		return nullptr;
	}
	return Runtime->FindRoomActor(Net->GetPlayer().RoomFile);
}

void UMRNetWorldSubsystem::OnRoomChange(const FMRNetRoomChange& C)
{
	if (AMRRuntimeRoom* Room = CurrentRuntimeRoom())
	{
		ApplyRoomChange(Room, C);  // (its rebuilt collision updates wading and steps: UMRRuntimeRooms)
	}
	else if (Rid)
	{
		// (a room still building takes it from GetRoomChanges when it's ready)
		UE_LOG(LogMeridian, Verbose, TEXT("MRNet: zone %d is authored; its room changes aren't drawn yet"), Rid);
	}
}

void UMRNetWorldSubsystem::ApplyRoomChange(AMRRuntimeRoom* Room, const FMRNetRoomChange& C)
{
	switch (C.Kind)
	{
	case FMRNetRoomChange::EKind::MoveSector:
		Room->MoveSector(C.Type, C.Id, C.Height, C.Speed);
		break;
	case FMRNetRoomChange::EKind::ChangeSector:
		Room->ChangeSector(C.Id, C.Depth, C.Scroll);
		break;
	case FMRNetRoomChange::EKind::ChangeTexture:
		if (!Room->ChangeTexture(C.Id, C.Texture, C.Flags))
		{
			if (UMRRuntimeRooms* Runtime = GetWorld()->GetSubsystem<UMRRuntimeRooms>())
			{
				Runtime->FetchTexture(Room->GetRoomFile(), C.Texture);
			}
		}
		break;
	}
}

void UMRNetWorldSubsystem::OnEffect()
{
	ApplyEffects();
}

void UMRNetWorldSubsystem::ApplyEffects(bool bEntered)
{
	const UMRNetSubsystem* Net = GetNet();
	if (!Net)
	{
		return;
	}
	const FMRNetEffects& E = Net->GetEffects();
	UpdateFrozen();  // move.c: no walking while paralyzed
	if (UMREnvironmentSubsystem* Env = GetWorld()->GetSubsystem<UMREnvironmentSubsystem>())
	{
		// room.kod GetRoomWeather: snow before rain before a sandstorm (fireworks: not drawn yet)
		const EMRWeatherKind Kind = E.Weather == MRMsg::EFFECT_SNOWING ? EMRWeatherKind::Snow
			: E.Weather == MRMsg::EFFECT_RAINING ? EMRWeatherKind::Rain
			: E.bSand ? EMRWeatherKind::Sand : EMRWeatherKind::None;
		Env->SetServerWeather(Kind, bEntered || FPlatformTime::Seconds() - RoomEnteredTime < WeatherOnEntrySeconds);
	}
}

void UMRNetWorldSubsystem::SendMovement(double Now)
{
	UMRNetSubsystem* Net = GetNet();
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	APlayerController* PC = GetPC();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FMRZoneInfo* Zone = Zones ? Zones->FindZone(Rid) : nullptr;
	if (!Pawn || !Zone)
	{
		return;
	}
	const FVector Pos = Pawn->GetActorLocation();
	const FIntPoint Kod = Zones->WorldToKod(Rid, Pos);
	const int32 MaxRow = MRMsg::KodFineness + FMath::FloorToInt(Zone->GridSizeRoo.Y / 16.0);
	const int32 MaxCol = MRMsg::KodFineness + FMath::FloorToInt(Zone->GridSizeRoo.X / 16.0);
	const bool bInRoom = Kod.X >= MRMsg::KodFineness && Kod.Y >= MRMsg::KodFineness && Kod.X < MaxRow && Kod.Y < MaxCol;

	if (!bInRoom)
	{
		// off the room's edge: ask to leave, at walking speed (move.c: slower players must get through too)
		if (Now - LastOffRoomTime >= OffRoomInterval)
		{
			Net->RequestMove(Kod.X, Kod.Y, MRMsg::SPEED_WALK);
			LastOffRoomTime = Now;
		}
		return;
	}

	if (Kod != LastSentKod && Now - LastMoveTime >= MoveInterval)
	{
		const bool bRunning = Pawn->GetVelocity().Size2D() > RunCms();
		Net->RequestMove(Kod.X, Kod.Y, bRunning ? MRMsg::SPEED_RUN : MRMsg::SPEED_WALK);
		LastSentKod = Kod;
		LastMoveTime = Now;
	}

	const int32 Angle = YawToKod(PC->GetControlRotation().Yaw);
	const int32 Diff = FMath::Abs(Angle - LastSentAngle);
	if (LastSentAngle < 0 || (FMath::Min(Diff, 4096 - Diff) >= TurnThreshold && Now - LastTurnTime >= MoveInterval))
	{
		Net->RequestTurn(Angle);
		LastSentAngle = Angle;
		LastTurnTime = Now;
	}
}

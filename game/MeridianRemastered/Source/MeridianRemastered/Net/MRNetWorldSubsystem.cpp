#include "Net/MRNetWorldSubsystem.h"

#include "Character/MRCharacter.h"
#include "Character/MRSpriteData.h"
#include "Core/MRUnits.h"
#include "Dom/JsonObject.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/MRGameMode.h"
#include "GameFramework/PlayerController.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/MRNetLook.h"
#include "Net/MRNetObject.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "World/MRRuntimeRooms.h"
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
		FOnMRNetEvent* Events[] = {&Net->OnPhaseChanged, &Net->OnRoomEntered};
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
	PlacePlayer(PrevRid == Rid);
	SetPawnFrozen(false);
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
	// by name first (NPCs share bodies), then by the body bitmap
	for (const TPair<FName, FMRMonsterDef>& Pair : Lib.Monsters)
	{
		if (!Object.Name.IsEmpty() && Pair.Value.Name.Equals(Object.Name, ESearchCase::IgnoreCase))
		{
			return Pair.Value.Look;
		}
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
	const FName Look = Object.IsCreature() ? LookFor(Object) : NAME_None;
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

void UMRNetWorldSubsystem::AttachBgfSprite(AMRNetObject* Actor, const FMRNetObject& Object)
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
	if (const TSharedPtr<const FMRBgf>* Known = Bgfs.Find(File))
	{
		Actor->SetBgfSprite(File, *Known);
		Actor->SetServerAnimation(Standing, Moving);
		return;
	}
	TWeakObjectPtr<UMRNetWorldSubsystem> Weak(this);
	TWeakObjectPtr<AMRNetObject> WeakActor(Actor);
	Cache->Fetch(File, [Weak, WeakActor, File, Standing, Moving](bool bOk, const TArray<uint8>& Bytes)
	{
		UMRNetWorldSubsystem* Self = Weak.Get();
		if (!Self)
		{
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
		if (AMRNetObject* A = WeakActor.Get())
		{
			A->SetBgfSprite(File, Bgf);
			A->SetServerAnimation(Standing, Moving);
		}
	});
}

void UMRNetWorldSubsystem::ClearObjects()
{
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
		return;
	}
	AMRNetObject* A = FindActor(Id);
	const FMRNetObject* O = GetNet() ? GetNet()->FindObject(Id) : nullptr;
	if (!A || !O)
	{
		return;
	}
	// another body (a corpse), a creature that stopped being one, or another bitmap: draw it again
	const FName Want = O->IsCreature() ? LookFor(*O) : NAME_None;
	const bool bRespawn = Want != A->GetLook() || A->IsStatic() == O->IsCreature()
		|| (Want.IsNone() && !A->IsShownByProp() && A->GetBgfName() != O->Icon.ToLower());
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

bool UMRNetWorldSubsystem::IsInSight(const AMRNetObject* A, FVector2D& OutScreen, double& OutDistance) const
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
	return !GetWorld()->LineTraceSingleByChannel(Hit, Camera, Middle, ECC_WorldStatic, Params)
		|| Hit.GetActor() && Hit.GetActor()->IsA<AMRNetObject>();
}

void UMRNetWorldSubsystem::UpdateAim()
{
	AimId = 0;
	AimStack.Reset();
	const APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}
	int32 W = 0, H = 0;
	PC->GetViewportSize(W, H);
	const FVector2D Centre(W * 0.5, H * 0.5);
	TArray<TPair<double, uint32>> Hits;
	for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : Actors)
	{
		const AMRNetObject* A = Pair.Value.Get();
		FVector2D Screen;
		double Dist = 0.0;
		if (!A || !IsInSight(A, Screen, Dist) || Dist > NameDistanceCm())
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

bool UMRNetWorldSubsystem::LookAtTarget()
{
	UMRNetSubsystem* Net = GetNet();
	const uint32 Id = TargetId ? TargetId : AimId;
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
	if (!Net || Net->GetPhase() != EMRNetPhase::InGame || !Rid || !Zones || !Pawn || Net->IsWaiting())
	{
		return;
	}
	// the server checks the square it last heard of: send where we are first (the move order is kept)
	const FIntPoint Kod = Zones->WorldToKod(Rid, Pawn->GetActorLocation());
	if (Kod != LastSentKod)
	{
		const bool bRunning = Pawn->GetVelocity().Size2D() > RunCms();
		Net->RequestMove(Kod.X, Kod.Y, bRunning ? MRMsg::SPEED_RUN : MRMsg::SPEED_WALK);
		LastSentKod = Kod;
		LastMoveTime = FPlatformTime::Seconds();
	}
	UE_LOG(LogMeridian, Log, TEXT("MRNet: go at (%d, %d) of zone %d: BP_REQ_GO"), Kod.X / MRMsg::KodFineness, Kod.Y / MRMsg::KodFineness, Rid);
	Net->RequestGo();
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

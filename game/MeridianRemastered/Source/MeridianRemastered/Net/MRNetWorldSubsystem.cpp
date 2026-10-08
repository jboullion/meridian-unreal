#include "Net/MRNetWorldSubsystem.h"

#include "Character/MRCharacter.h"
#include "Character/MRSpriteData.h"
#include "Core/MRUnits.h"
#include "Dom/JsonObject.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/MRGameMode.h"
#include "GameFramework/PlayerController.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/MRNetLook.h"
#include "Net/MRNetObject.h"
#include "Net/MRNetSubsystem.h"
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
	Rid = Zones->RidForRoom(P.RoomFile);
	UE_LOG(LogMeridian, Log, TEXT("MRNet: entered %s (%s) -> zone %d, %d objects"), *P.RoomFile, *P.RoomName, Rid, Net->GetObjects().Num());
	if (!Rid)
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: no zone is built from %s; staying put"), *P.RoomFile);
		// before the first room there is no pawn: say why on the login screen (Cancel logs off)
		Net->SetStatus(FString::Printf(TEXT("%s isn't in this version of the game yet."), *P.RoomName));
		return;
	}
	PlacePlayer();
	ApplySelfLook();
	for (const TPair<uint32, FMRNetObject>& Pair : Net->GetObjects())
	{
		SpawnObject(Pair.Value);
	}
	LastSentKod = FIntPoint(-1, -1);
	LastSentAngle = -1;
	UpdateScreens();
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

void UMRNetWorldSubsystem::PlacePlayer()
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
	if (AMRPlayerState* PS = PC->GetPlayerState<AMRPlayerState>())
	{
		PS->SetZoneId(Rid);  // the minimap, music and moods follow the zone
	}
	if (APawn* Pawn = PC->GetPawn())
	{
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
	if (!Net || !Zones || !Rid || Object.Id == Net->GetPlayer().Id || !Object.IsCreature() || FindActor(Object.Id))
	{
		return;
	}
	const FName Look = LookFor(Object);
	if (Look.IsNone())
	{
		UE_LOG(LogMeridian, Log, TEXT("MRNet: no sprite for %s (%s); not drawn"), *Object.Name, *Object.Icon);
		return;
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
	FMRSpriteAppearance A;
	if (MRNetLook::AppearanceFromObject(Object, A))
	{
		Actor->SetAppearance(A);
	}
	Actor->Place(Floor, Object.Angle);
	Actors.Add(Object.Id, Actor);
}

void UMRNetWorldSubsystem::ClearObjects()
{
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
	if (A && O && LookFor(*O) != A->GetLook())
	{
		A->Destroy();
		Actors.Remove(Id);
		SpawnObject(*O);
	}
	else if (FMRSpriteAppearance Look; A && O && MRNetLook::AppearanceFromObject(*O, Look))
	{
		A->SetAppearance(Look);  // the same body: new face parts or colours
	}
}

void UMRNetWorldSubsystem::OnObjectRemoved(uint32 Id)
{
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

// ------------------------------------------------------------------------------ movement up

void UMRNetWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UMRNetSubsystem* Net = GetNet();
	if (Net && Net->GetPhase() == EMRNetPhase::InGame && Rid)
	{
		SendMovement(FPlatformTime::Seconds());
	}
}

void UMRNetWorldSubsystem::RequestGo()
{
	UMRNetSubsystem* Net = GetNet();
	UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	APlayerController* PC = GetPC();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Net || Net->GetPhase() != EMRNetPhase::InGame || !Rid || !Zones || !Pawn)
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

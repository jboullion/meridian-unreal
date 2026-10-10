#include "Tests/MRNetTest.h"

#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UnrealMeridian.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Character/MRCharacter.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Net/MRAssetCache.h"
#include "Net/MRCharInfo.h"
#include "Net/MRProtocol.h"
#include "Net/MRResources.h"
#include "Net/MRNetWorld.h"
#include "Net/MRNetObject.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorldSubsystem.h"
#include "TimerManager.h"
#include "UI/MRUISubsystem.h"
#include "UI/MRInventorySource.h"
#include "UI/SMRLookDialog.h"
#include "UI/SMRTradeDialog.h"
#include "UI/SMRSocial.h"
#include "UI/SMROptions.h"
#include "UI/MRInventorySource.h"
#include "Engine/LocalPlayer.h"
#include "UnrealClient.h"
#include "Zones/MRZoneSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Core/MRUnits.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "World/MRRuntimeRoom.h"
#include "World/MRRuntimeRooms.h"
#include "Audio/MRAudioSubsystem.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

namespace
{
	constexpr double StepTimeout = 45.0;

	UMRNetSubsystem* NetOf(const APlayerController* PC)
	{
		const UGameInstance* GI = PC ? PC->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
	}

	/** A character name the server hasn't seen: letters only. */
	FString RandomName()
	{
		FString Name = TEXT("Unreal");
		for (int32 i = 0; i < 6; ++i)
		{
			Name.AppendChar(TEXT('a') + FMath::RandRange(0, 25));
		}
		return Name;
	}
}

bool UMRNetTest::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRNetTest"));
}

void UMRNetTest::Start(APlayerController* InController)
{
	Controller = InController;
	UMRNetSubsystem* Net = NetOf(InController);
	FString ServerPart = TEXT("Local"), User, Pass;
	FParse::Value(FCommandLine::Get(), TEXT("MRServer="), ServerPart);
	FParse::Value(FCommandLine::Get(), TEXT("MRNetUser="), User);
	FParse::Value(FCommandLine::Get(), TEXT("MRNetPass="), Pass);
	FParse::Value(FCommandLine::Get(), TEXT("MRNetHold="), HoldSeconds);
	TestPassword = Pass;
	FParse::Value(FCommandLine::Get(), TEXT("MRNetPal="), PalName);
	bDeath = FParse::Param(FCommandLine::Get(), TEXT("MRNetDeath"));
	int32 Server = INDEX_NONE;
	for (int32 i = 0; Net && i < Net->GetServers().Num(); ++i)
	{
		if (Net->GetServers()[i].Name.Contains(ServerPart))
		{
			Server = i;
			break;
		}
	}
	if (!Net || Server == INDEX_NONE || User.IsEmpty() || Pass.IsEmpty())
	{
		Fail(TEXT("setup: need a server matching -MRServer, -MRNetUser and -MRNetPass"));
		Finish();
		return;
	}
	UE_LOG(LogMeridian, Display, TEXT("MRNetTest: logging in to %s as %s"), *Net->GetServers()[Server].Name, *User);
	Net->OnDescription.AddWeakLambda(this, [this]() { ++Descriptions; });
	Net->Connect(Server, User, Pass);
	Step = EStep::Login;
	StepStart = FPlatformTime::Seconds();
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRNetTest::Tick), 0.25f, true);
}

void UMRNetTest::SendNewCharacter(UMRNetSubsystem* Net)
{
	const FMRCharInfo& Info = Net->GetCharInfo();
	FMRNewCharacter C;
	C.Name = RandomName();
	C.Description = TEXT("Made by the Unreal client's online test.");
	// not the defaults: the second of each part, the second skin, a hair colour from the list's end
	C.bFemale = true;
	C.Hair = 1;
	C.Eyes = 1;
	C.Nose = 1;
	C.Mouth = 1;
	C.Skin = 1;
	C.HairColour = Info.HairXlats.Num() - 4;
	MRCharInfo::ClampParts(C, Info);
	const int32 Mage[] = {40, 50, 45, 15, 45, 25};  // the creator's Mage preset (data/ui/char_create.json)
	FMemory::Memcpy(C.Stats, Mage, sizeof(Mage));
	// one 10-point spell (not Shal'ille or Qor) and one 10-point skill
	if (const FMRCharAbility* Sp = Info.Spells.FindByPredicate([](const FMRCharAbility& A) { return A.Cost == 10 && A.School > 2; }))
	{
		C.Spells.Add(Sp->Num);
	}
	if (const FMRCharAbility* Sk = Info.Skills.FindByPredicate([](const FMRCharAbility& A) { return A.Cost == 10; }))
	{
		C.Skills.Add(Sk->Num);
	}
	FString Why;
	if (MRCharInfo::Validate(C, Info, Why) != MRCharInfo::EProblem::None)
	{
		Fail(FString::Printf(TEXT("the test character doesn't validate: %s"), *Why));
	}
	const FMRCharFaces& F = Info.Faces(true);
	ExpectedParts = {{1, F.Head.Bgf}, {13, F.Hair[C.Hair].Bgf}, {11, F.Eyes[C.Eyes].Bgf}, {14, F.Noses[C.Nose].Bgf}, {12, F.Mouths[C.Mouth].Bgf}};
	ExpectedSkinXlat = Info.SkinXlats[C.Skin];
	ExpectedHairXlat = Info.HairXlats[C.HairColour];
	UE_LOG(LogMeridian, Display, TEXT("MRNetTest: the server offers %d male / %d female hair, %d spells, %d skills; creating %s (female, %s %s %s %s, skin %d, hair colour %d, %d spell, %d skill)"),
		Info.Male.Hair.Num(), Info.Female.Hair.Num(), Info.Spells.Num(), Info.Skills.Num(), *C.Name, *F.Hair[C.Hair].Bgf, *F.Eyes[C.Eyes].Bgf,
		*F.Noses[C.Nose].Bgf, *F.Mouths[C.Mouth].Bgf, ExpectedSkinXlat, ExpectedHairXlat, C.Spells.Num(), C.Skills.Num());
	Net->CreateCharacter(C);
	bCreated = true;
}

void UMRNetTest::CheckLook(UMRNetSubsystem* Net)
{
	const FMRNetObject* Self = Net->GetSelf();
	if (!bCreated || !Self)
	{
		return;
	}
	TArray<FString> Wrong;
	for (const TPair<uint8, FString>& E : ExpectedParts)
	{
		const FMRNetOverlay* O = Self->OverlayParts.FindByPredicate([&E](const FMRNetOverlay& V) { return V.Hotspot == E.Key; });
		if (!O || O->Bgf != E.Value)
		{
			Wrong.Add(FString::Printf(TEXT("hotspot %d: %s, not %s"), E.Key, O ? *O->Bgf : TEXT("none"), *E.Value));
		}
		else if (E.Key == 1 && O->Xlat != ExpectedSkinXlat)
		{
			Wrong.Add(FString::Printf(TEXT("skin %d, not %d"), O->Xlat, ExpectedSkinXlat));
		}
		else if (E.Key == 13 && O->Xlat != ExpectedHairXlat)
		{
			Wrong.Add(FString::Printf(TEXT("hair colour %d, not %d"), O->Xlat, ExpectedHairXlat));
		}
	}
	if (Wrong.IsEmpty())
	{
		Pass(TEXT("the server shows the new character's chosen face, hair and colours"));
	}
	else
	{
		Fail(FString::Printf(TEXT("the new character's look: %s"), *FString::Join(Wrong, TEXT("; "))));
	}
}

void UMRNetTest::Pass(const FString& What)
{
	++Steps;
	++Passed;
	UE_LOG(LogMeridian, Display, TEXT("MRNetTest: PASS %s"), *What);
}

void UMRNetTest::Fail(const FString& What)
{
	++Steps;
	UE_LOG(LogMeridian, Display, TEXT("MRNetTest: FAIL %s"), *What);
}

void UMRNetTest::Finish()
{
	Step = EStep::Done;
	UE_LOG(LogMeridian, Display, TEXT("MRNetTest: DONE %d/%d"), Passed, Steps);
	if (APlayerController* PC = Controller.Get())
	{
		PC->GetWorldTimerManager().ClearTimer(Timer);
		PC->ConsoleCommand(TEXT("quit"));
	}
}

void UMRNetTest::LogCreatures() const
{
	const APlayerController* PC = Controller.Get();
	FString List;
	int32 N = 0;
	for (TActorIterator<AMRNetObject> It(PC->GetWorld()); It; ++It)
	{
		List += FString::Printf(TEXT("%s%s (%s)"), N++ ? TEXT(", ") : TEXT(""), *It->GetObjectName(), *It->GetLook().ToString());
	}
	UE_LOG(LogMeridian, Display, TEXT("MRNetTest: draws %d creatures: %s"), N, *List);
}

namespace
{
	/**
	 * Where to stand on a door's square: a third of a square from its middle toward the room's, still on
	 * the square (Kod's "go" takes the door on the square the player stands on). A door square's middle
	 * is often in the frame, outside every sector, and the server snaps such a move back.
	 */
	FVector DoorStandPoint(UMRZoneSubsystem* Zones, const FMRZoneInfo& Zone, int32 Row, int32 Col, double HalfHeight)
	{
		const FVector Middle = Zones->GridToWorld(Zone.Rid, Row, Col, false);
		const FVector RoomMiddle = Zone.Origin + MRUnits::RooToLocal(Zone.GridSizeRoo * 0.5);
		FVector At = Middle + (RoomMiddle - Middle).GetSafeNormal2D() * (MRUnits::CmPerSquare / 3.0);
		if (!Zones->TraceFloor(At))
		{
			At = Zones->GridToWorld(Zone.Rid, Row, Col, true);
		}
		return At + FVector(0.0, 0.0, HalfHeight + 2.0);
	}
}

bool UMRNetTest::TakeExit()
{
	// stand on the first exit of this zone that leads to another zone we have built
	APlayerController* PC = Controller.Get();
	UMRNetWorldSubsystem* NetWorld = PC ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	UMRZoneSubsystem* Zones = PC ? PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FMRZoneInfo* Zone = Zones && NetWorld ? Zones->FindZone(NetWorld->GetRid()) : nullptr;
	if (!Pawn || !Zone)
	{
		return false;
	}
	for (const FMRZoneExit& E : Zone->Exits)
	{
		if (!E.bLocked && Zones->FindZone(E.DestRid))
		{
			const FVector At = DoorStandPoint(Zones, *Zone, E.Row, E.Col, Pawn->GetSimpleCollisionHalfHeight());
			UE_LOG(LogMeridian, Display, TEXT("MRNetTest: stepping onto the exit (%d, %d) of zone %d toward %d"), E.Row, E.Col, Zone->Rid, E.DestRid);
			if (!Pawn->TeleportTo(At, Pawn->GetActorRotation(), false, true))
			{
				return false;
			}
			NetWorld->RequestGo();  // the space bar: doors wait for "go"
			return true;
		}
	}
	// no door to another zone of ours (the Outskirts have none): walk off an edge instead
	for (const FMREdgeExit& E : Zone->EdgeExits)
	{
		if (Zones->FindZone(E.DestRid) && StepOffEdge(static_cast<uint8>(E.Edge)))
		{
			return true;
		}
	}
	return false;
}

bool UMRNetTest::StepOffEdge(uint8 Edge)
{
	APlayerController* PC = Controller.Get();
	UMRNetWorldSubsystem* NetWorld = PC ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	UMRZoneSubsystem* Zones = PC ? PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FMRZoneInfo* Zone = Zones && NetWorld ? Zones->FindZone(NetWorld->GetRid()) : nullptr;
	if (!Pawn || !Zone)
	{
		return false;
	}
	// just past the edge where there is floor: Kod takes an edge exit only for a move outside the room's
	// box that still lands in one of its sectors (user.kod UserMove, room.kod SomethingMoved), as a road
	// drawn past the edge. Search along the edge, nearest to where we stand first.
	const FVector2D Size = Zone->GridSizeRoo;
	const FVector2D Here = MRUnits::LocalToRoo(Pawn->GetActorLocation() - Zone->Origin);
	const double Out = MRUnits::RooPerSquare / 4;
	const bool bAcross = Edge == static_cast<uint8>(EMREdge::North) || Edge == static_cast<uint8>(EMREdge::South);
	const double Along = bAcross ? Here.X : Here.Y;
	const double Length = bAcross ? Size.X : Size.Y;
	FVector At;
	bool bFound = false;
	for (double Offset = 0.0; Offset <= Length && !bFound; Offset += MRUnits::RooPerSquare / 2)
	{
		for (const double Sign : {1.0, -1.0})
		{
			const double A = FMath::Clamp(Along + Sign * Offset, 0.0, Length);
			FVector2D Target;
			switch (static_cast<EMREdge>(Edge))
			{
			case EMREdge::North: Target = FVector2D(A, -Out); break;
			case EMREdge::South: Target = FVector2D(A, Size.Y + Out); break;
			case EMREdge::East: Target = FVector2D(Size.X + Out, A); break;
			default: Target = FVector2D(-Out, A); break;
			}
			FVector P = Zone->Origin + MRUnits::RooToLocal(Target) + FVector(0.0, 0.0, Pawn->GetActorLocation().Z - Zone->Origin.Z);
			if (Zones->TraceFloor(P))
			{
				At = P + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0);
				bFound = true;
				break;
			}
		}
	}
	static const TCHAR* Names[] = {TEXT("north"), TEXT("south"), TEXT("east"), TEXT("west")};
	if (!bFound)
	{
		UE_LOG(LogMeridian, Display, TEXT("MRNetTest: travel: no floor past the %s edge of zone %d"), Names[FMath::Min<int32>(Edge, 3)], Zone->Rid);
		return false;
	}
	UE_LOG(LogMeridian, Display, TEXT("MRNetTest: travel: stepping off the %s edge of zone %d"), Names[FMath::Min<int32>(Edge, 3)], Zone->Rid);
	return Pawn->TeleportTo(At, Pawn->GetActorRotation(), false, true);
}

bool UMRNetTest::HopToward(int32 TargetRid)
{
	APlayerController* PC = Controller.Get();
	UMRNetWorldSubsystem* NetWorld = PC ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	UMRZoneSubsystem* Zones = PC ? PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const int32 From = NetWorld ? NetWorld->GetRid() : 0;
	if (!Pawn || !Zones || !Zones->FindZone(From))
	{
		return false;
	}
	// breadth first over our zones; remember each zone's first hop
	TMap<int32, int32> FirstHop;
	TArray<int32> Queue = {From};
	FirstHop.Add(From, 0);
	for (int32 i = 0; i < Queue.Num() && !FirstHop.Contains(TargetRid); ++i)
	{
		const FMRZoneInfo* Z = Zones->FindZone(Queue[i]);
		TArray<int32> Next;
		for (const FMRZoneExit& E : Z->Exits)
		{
			if (!E.bLocked)
			{
				Next.Add(E.DestRid);
			}
		}
		for (const FMREdgeExit& E : Z->EdgeExits)
		{
			Next.Add(E.DestRid);
		}
		for (const int32 N : Next)
		{
			if (Zones->FindZone(N) && !FirstHop.Contains(N))
			{
				FirstHop.Add(N, i == 0 ? N : FirstHop[Queue[i]]);
				Queue.Add(N);
			}
		}
	}
	const int32* Hop = FirstHop.Find(TargetRid);
	if (!Hop || !*Hop)
	{
		return false;
	}
	const FMRZoneInfo* Z = Zones->FindZone(From);
	HopAt = FPlatformTime::Seconds();
	// a door's squares in turn (one may be in the wall: the server snaps the move back and won't go)
	TArray<const FMRZoneExit*> Doors;
	for (const FMRZoneExit& E : Z->Exits)
	{
		if (!E.bLocked && E.DestRid == *Hop)
		{
			Doors.Add(&E);
		}
	}
	if (Doors.Num() > 0)
	{
		const FMRZoneExit& E = *Doors[HopTry % Doors.Num()];
		{
			const FVector At = DoorStandPoint(Zones, *Z, E.Row, E.Col, Pawn->GetSimpleCollisionHalfHeight());
			UE_LOG(LogMeridian, Display, TEXT("MRNetTest: travel: the exit (%d, %d) of zone %d toward %d"), E.Row, E.Col, From, *Hop);
			Pawn->TeleportTo(At, Pawn->GetActorRotation(), false, true);
			NetWorld->RequestGo();
			return true;
		}
	}
	for (const FMREdgeExit& E : Z->EdgeExits)
	{
		if (E.DestRid == *Hop)
		{
			return StepOffEdge(static_cast<uint8>(E.Edge));
		}
	}
	return false;
}

void UMRNetTest::CheckRuntimeRoom()
{
	APlayerController* PC = Controller.Get();
	UMRNetSubsystem* Net = NetOf(PC);
	UMRNetWorldSubsystem* NetWorld = PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	UMRZoneSubsystem* Zones = PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	UMRRuntimeRooms* Runtime = PC->GetWorld()->GetSubsystem<UMRRuntimeRooms>();
	const FString Room = Net->GetPlayer().RoomFile;
	const AMRRuntimeRoom* Actor = Runtime ? Runtime->FindRoomActor(Room) : nullptr;
	if (Actor)
	{
		Pass(FString::Printf(TEXT("built %s (%s) from the server's files as zone %d"), *Room, *Net->GetPlayer().RoomName, NetWorld->GetRid()));
	}
	else
	{
		Fail(FString::Printf(TEXT("travel: %s has no runtime room"), *Room));
	}
	// standing on its floor: the capsule's bottom on the surface right under it (the pawn itself ignored)
	const ACharacter* Char = Cast<ACharacter>(PC->GetPawn());
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRNetTestFloor), true, Char);
	const FVector At = Char ? Char->GetActorLocation() : FVector::ZeroVector;
	const double Feet = Char ? At.Z - Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0;
	if (Char && PC->GetWorld()->LineTraceSingleByChannel(Hit, At, At - FVector(0, 0, 1000), ECC_WorldStatic, Params)
		&& FMath::Abs(Feet - Hit.ImpactPoint.Z) < 10.0 && Hit.GetActor() == Actor)
	{
		const FMRNetObject* Self = Net->GetSelf();
		Pass(FString::Printf(TEXT("the pawn stands on the runtime room's floor at (%d, %d), %.1f cm off"), Self ? Self->KodRow : 0,
			Self ? Self->KodCol : 0, FMath::Abs(Feet - Hit.ImpactPoint.Z)));
	}
	else
	{
		Fail(FString::Printf(TEXT("travel: the pawn isn't on the runtime room's floor (feet %.0f, below it: %s at %.0f)"), Feet,
			*GetNameSafe(Hit.GetActor()), Hit.ImpactPoint.Z));
	}
}

void UMRNetTest::CheckRoomLight()
{
	// M_RuntimeRoom draws with the server's light: the room's ambient (BP_PLAYER, BP_LIGHT_AMBIENT) and the player's
	APlayerController* PC = Controller.Get();
	UMRNetSubsystem* Net = NetOf(PC);
	const UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/Game/Generated/Environment/Materials/MPC_Environment.MPC_Environment"));
	if (!Collection)
	{
		Fail(TEXT("light: no MPC_Environment"));
		return;
	}
	const float Ambient = UKismetMaterialLibrary::GetScalarParameterValue(PC->GetWorld(), const_cast<UMaterialParameterCollection*>(Collection), TEXT("RoomAmbient"));
	const float Player = UKismetMaterialLibrary::GetScalarParameterValue(PC->GetWorld(), const_cast<UMaterialParameterCollection*>(Collection), TEXT("PlayerLight"));
	const FMRNetPlayer& P = Net->GetPlayer();
	if (FMath::IsNearlyEqual(Ambient, P.AmbientLight / 255.f, 1e-3f) && FMath::IsNearlyEqual(Player, P.PlayerLight / 255.f, 1e-3f))
	{
		Pass(FString::Printf(TEXT("the runtime room is lit by the server's light: ambient %d, player %d"), P.AmbientLight, P.PlayerLight));
	}
	else
	{
		Fail(FString::Printf(TEXT("light: the room draws with ambient %.3f and player %.3f, the server says %d and %d"), Ambient, Player,
			P.AmbientLight, P.PlayerLight));
	}
}

void UMRNetTest::CheckServerSounds()
{
	// the server's music and sounds (BP_PLAY_MUSIC, BP_PLAY_WAVE) are found and made ready to play: imported or decoded
	APlayerController* PC = Controller.Get();
	const UMRAudioSubsystem* Audio = PC ? PC->GetWorld()->GetSubsystem<UMRAudioSubsystem>() : nullptr;
	if (!Audio)
	{
		Fail(TEXT("sound: no audio subsystem"));
		return;
	}
	if (Audio->GetServerSoundsAsked() > 0 && Audio->GetServerSoundsPlayed() > 0 && !Audio->GetMusicFile().IsEmpty())
	{
		Pass(FString::Printf(TEXT("the server's sounds play: %d of %d ready, music %s"), Audio->GetServerSoundsPlayed(),
			Audio->GetServerSoundsAsked(), *Audio->GetMusicFile()));
	}
	else
	{
		Fail(FString::Printf(TEXT("sound: %d of the server's %d sounds ready, music \"%s\""), Audio->GetServerSoundsPlayed(),
			Audio->GetServerSoundsAsked(), *Audio->GetMusicFile()));
	}
}

void UMRNetTest::CheckCreaturesDrawn()
{
	APlayerController* PC = Controller.Get();
	UMRNetSubsystem* Net = NetOf(PC);
	UMRNetWorldSubsystem* NetWorld = PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>();
	// every creature in it is drawn: with a sprite of ours, or else from the server's bitmap
	TArray<FString> Drawn, Missing;
	for (const TPair<uint32, FMRNetObject>& Pair : Net->GetObjects())
	{
		if (Pair.Key == Net->GetPlayer().Id || !Pair.Value.IsCreature())
		{
			continue;
		}
		const AMRNetObject* A = NetWorld->FindActor(Pair.Key);
		if (A && (!A->GetLook().IsNone() || A->GetBgfName().Len() > 0))
		{
			Drawn.Add(FString::Printf(TEXT("%s (%s)"), *Pair.Value.Name, A->GetLook().IsNone() ? *A->GetBgfName() : *A->GetLook().ToString()));
		}
		else
		{
			Missing.Add(FString::Printf(TEXT("%s (%s)"), *Pair.Value.Name, *Pair.Value.Icon));
		}
	}
	if (Missing.IsEmpty())
	{
		Pass(FString::Printf(TEXT("every creature in the runtime room is drawn: %s"), Drawn.IsEmpty() ? TEXT("none there") : *FString::Join(Drawn, TEXT(", "))));
	}
	else
	{
		Fail(FString::Printf(TEXT("travel: creatures not drawn: %s"), *FString::Join(Missing, TEXT(", "))));
	}
}

void UMRNetTest::Tick()
{
	APlayerController* PC = Controller.Get();
	UMRNetSubsystem* Net = NetOf(PC);
	UMRNetWorldSubsystem* NetWorld = PC ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	if (!Net || !NetWorld || Step == EStep::Done)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	auto Advance = [this, Now](EStep Next)
	{
		Step = Next;
		StepStart = Now;
		bAsked = false;
	};
	const bool bTimedOut = Now - StepStart > StepTimeout;

	switch (Step)
	{
	case EStep::Login:
		if (Net->GetPhase() == EMRNetPhase::Characters && Net->GetStatus().IsEmpty())
		{
			// (after a refused name the status clears and the error says why: try another)
			const FMRCharacterSlot* Named = Net->GetCharacters().FindByPredicate([](const FMRCharacterSlot& C) { return !C.bNeedsCreation; });
			const FMRCharacterSlot* Empty = Net->GetCharacters().FindByPredicate([](const FMRCharacterSlot& C) { return C.bNeedsCreation; });
			if (Named && FApp::CanEverRender() && HoldSeconds > 0.f && (CharacterShotAt == 0.0 || Now - CharacterShotAt < 2.0))
			{
				// -Render -Hold: a picture of the character page first (once it has laid out), then a moment for the capture
				if (CharacterShotAt == 0.0)
				{
					CharacterShotAt = Now;
				}
				else if (Now - CharacterShotAt > 1.0 && !bAsked)
				{
					bAsked = true;
					const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), TEXT("characters.png"));
					FScreenshotRequest::RequestScreenshot(File, true, false);
					UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
				}
			}
			else if (Named)
			{
				Pass(FString::Printf(TEXT("logged in: %d character slots, playing %s"), Net->GetCharacters().Num(), *Named->Name));
				Net->UseCharacter(Named->Id);
				Advance(EStep::Enter);
			}
			else if (Empty && CreateTries < 3)
			{
				// the creator's path: ask for the server's options first (SendNewCharacter below)
				UE_LOG(LogMeridian, Display, TEXT("MRNetTest: no characters yet; asking the server for its character options"));
				++CreateTries;
				Net->RequestCharInfo(Empty->Id);
			}
			else
			{
				Fail(FString::Printf(TEXT("create a character: %s"), *Net->GetLastError()));
				Finish();
			}
		}
		else if (Net->GetPhase() == EMRNetPhase::Creating && Net->GetCharInfo().IsValid() && !Net->IsSubmittingCharacter())
		{
			// the options are in (or the server refused a name: try another)
			if (CreateTries++ < 4)
			{
				SendNewCharacter(Net);
			}
			else
			{
				Fail(FString::Printf(TEXT("create a character: %s"), *Net->GetLastError()));
				Finish();
			}
		}
		else if (Net->GetPhase() == EMRNetPhase::Entering || Net->GetPhase() == EMRNetPhase::InGame)
		{
			Pass(TEXT("logged in and created a character through the creator's path"));
			Advance(EStep::Enter);
		}
		else if ((Net->GetPhase() == EMRNetPhase::Offline && !Net->GetLastError().IsEmpty()) || bTimedOut)
		{
			Fail(FString::Printf(TEXT("login: %s"), Net->GetLastError().IsEmpty() ? TEXT("timed out") : *Net->GetLastError()));
			Finish();
		}
		break;

	case EStep::Enter:
		if (Net->GetPhase() == EMRNetPhase::InGame && NetWorld->GetRid() && PC->GetPawn())
		{
			StartRid = NetWorld->GetRid();
			const FMRNetObject* Self = Net->GetSelf();
			Pass(FString::Printf(TEXT("entered %s (%s) as zone %d at (%d, %d), %d objects in the room"), *Net->GetPlayer().RoomFile,
				*Net->GetPlayer().RoomName, StartRid, Self ? Self->KodRow : 0, Self ? Self->KodCol : 0, Net->GetObjects().Num()));
			CheckLook(Net);
			if (NetWorld->DoesRoomMatchServer())
			{
				Pass(FString::Printf(TEXT("zone %d is built from the server's %s (security %08x)"), StartRid, *Net->GetPlayer().RoomFile,
					Net->GetPlayer().RoomSecurity & 0x0FFFFFFF));
			}
			else
			{
				Fail(FString::Printf(TEXT("zone %d isn't built from the server's %s"), StartRid, *Net->GetPlayer().RoomFile));
			}
			Advance(EStep::Say);
		}
		else if (bTimedOut || Net->GetPhase() == EMRNetPhase::Offline)
		{
			Fail(FString::Printf(TEXT("enter the game (phase %d, room %s, zone %d)"), static_cast<int32>(Net->GetPhase()),
				*Net->GetPlayer().RoomFile, NetWorld->GetRid()));
			Finish();
		}
		break;

	case EStep::Say:
		if (!bAsked && Now - StepStart > 2.0)
		{
			SayText = FString::Printf(TEXT("Hello from Unreal %d"), FMath::RandRange(100, 999));
			Net->Say(SayText);
			bAsked = true;
		}
		else if (bAsked && Net->GetChat().ContainsByPredicate([this](const FMRChatLine& L) { return L.Text.Contains(SayText); }))
		{
			Pass(FString::Printf(TEXT("said \"%s\" and heard it back"), *SayText));
			LogCreatures();
			Advance(HoldSeconds > 0.f ? EStep::Hold : EStep::Exit);
		}
		else if (bTimedOut)
		{
			Fail(TEXT("chat: our line never came back"));
			Advance(EStep::Exit);
		}
		break;

	case EStep::Hold:
	{
		// pace a square east and back every few seconds, so the other client sees us walk
		const double T = Now - StepStart;
		if (T > HoldSeconds)
		{
			LogCreatures();
			if (UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr)
			{
				UI->SetGameMenuOpen(false);
				UI->SetInventoryOpen(false);
			}
			Advance(EStep::Exit);
			break;
		}
		if (T > 3.0 * (HoldMoves + 1))
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				const double Dx = (HoldMoves % 2 == 0 ? 1.0 : -1.0) * 220.0;
				Pawn->TeleportTo(Pawn->GetActorLocation() + FVector(Dx, 0.0, 0.0), Pawn->GetActorRotation(), false, true);
			}
			++HoldMoves;
		}
		if (FApp::CanEverRender() && T > 5.0 + 8.0 * Shots && Shots < 6)
		{
			// two of the world, the dialog on the server's stats, spells and the (online) inventory, then the Escape menu
			UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
			if (UI && Shots >= 2 && Shots <= 4)
			{
				UI->SetInventoryOpen(true);
				UI->SetInventoryTab(Shots == 2 ? 3 : Shots == 3 ? 1 : 0);
			}
			else if (UI && Shots == 5)
			{
				UI->SetInventoryOpen(false);
				UI->SetGameMenuOpen(true);
			}
			const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), FString::Printf(TEXT("online_%d.png"), Shots));
			// a moment for the dialog to lay out before the capture
			PC->GetWorldTimerManager().SetTimerForNextTick([File]() { FScreenshotRequest::RequestScreenshot(File, true, false); });
			UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
			++Shots;
		}
		break;
	}

	case EStep::Exit:
		if (!bAsked && Now - StepStart > 1.0)
		{
			bAsked = true;
			if (!TakeExit())
			{
				Fail(FString::Printf(TEXT("exit: zone %d has no exit to a zone we have"), NetWorld->GetRid()));
				Advance(EStep::Assets);
			}
		}
		else if (bAsked && NetWorld->GetRid() && NetWorld->GetRid() != StartRid)
		{
			Pass(FString::Printf(TEXT("took an exit from zone %d to zone %d (%s)"), StartRid, NetWorld->GetRid(), *Net->GetPlayer().RoomName));
			Advance(EStep::Assets);
		}
		else if (bTimedOut)
		{
			Fail(FString::Printf(TEXT("exit: still in zone %d"), NetWorld->GetRid()));
			Advance(EStep::Assets);
		}
		break;

	case EStep::Assets:
		if (!bAsked)
		{
			bAsked = true;
			const FString Room = Net->GetPlayer().RoomFile.ToLower();
			const uint32 Security = Net->GetPlayer().RoomSecurity;
			TWeakObjectPtr<UMRNetTest> Weak(this);
			Net->GetAssets()->Fetch(Room, [Weak, Room, Security](bool bOk, const TArray<uint8>& Bytes)
			{
				if (UMRNetTest* Self = Weak.Get())
				{
					Self->bAssetDone = true;
					Self->bAssetOk = bOk && MRNetRead::SecurityMatches(MRNetRead::RooSecurity(Bytes), Security);
					Self->AssetResult = FString::Printf(TEXT("%s: %s, %d bytes, security %08x (server %08x)"), *Room, bOk ? TEXT("downloaded") : TEXT("failed"),
						Bytes.Num(), MRNetRead::RooSecurity(Bytes), Security);
				}
			});
		}
		else if (bAssetDone)
		{
			TArray<uint8> Again;
			if (bAssetOk && Net->GetAssets()->Load(Net->GetPlayer().RoomFile, Again))
			{
				Pass(FString::Printf(TEXT("asset cache (%d files listed): %s, then read back from the cache"), Net->GetAssets()->NumListed(), *AssetResult));
			}
			else
			{
				Fail(FString::Printf(TEXT("asset cache: %s"), *AssetResult));
			}
			const FMRNetUser* Me = Net->GetUsers().Find(Net->GetPlayer().Id);
			if (Me)
			{
				Pass(FString::Printf(TEXT("the players list has %d players, us among them (%s)"), Net->GetUsers().Num(), *Me->Name));
			}
			else
			{
				Fail(FString::Printf(TEXT("players list: %d players, not us"), Net->GetUsers().Num()));
			}
			Advance(EStep::Reload);
		}
		else if (bTimedOut)
		{
			Fail(TEXT("asset cache: no answer"));
			Advance(EStep::Reload);
		}
		break;

	case EStep::Reload:
		if (!bAsked)
		{
			bAsked = true;
			bSaid = false;
			RoomsBefore = Net->GetRoomsEntered();
			Net->ReloadData();
		}
		else if (!bSaid && Net->GetRoomsEntered() > RoomsBefore && !Net->IsWaiting() && NetWorld->GetRid())
		{
			// the room is back: is the session still fine?
			bSaid = true;
			SayText = FString::Printf(TEXT("Reloaded %d"), FMath::RandRange(100, 999));
			Net->Say(SayText);
		}
		else if (bSaid && Net->GetChat().ContainsByPredicate([this](const FMRChatLine& L) { return L.Text.Contains(SayText); }))
		{
			Pass(FString::Printf(TEXT("reloaded the data: the room came back (%d objects, %d players) and \"%s\" was heard"), Net->GetObjects().Num(),
				Net->GetUsers().Num(), *SayText));
			Advance(EStep::Look);
		}
		else if (bTimedOut || Net->GetPhase() == EMRNetPhase::Offline)
		{
			Fail(FString::Printf(TEXT("reload: %s"), Net->GetPhase() == EMRNetPhase::Offline ? *Net->GetLastError()
				: bSaid ? TEXT("our line never came back") : TEXT("the room never came back")));
			Advance(Net->GetPhase() == EMRNetPhase::Offline ? EStep::Logoff : EStep::Look);
		}
		break;

	case EStep::Look:
	{
		const uint32 Me = Net->GetPlayer().Id;
		const FMRNetDescription& D = Net->GetDescription();
		UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
		if (bTimedOut)
		{
			Fail(FString::Printf(TEXT("look: stuck at stage %d"), LookStage));
			Advance(EStep::Items);
			break;
		}
		// first to the Inn of Raza (zone 301), where Marcus the innkeeper stands
		constexpr int32 Inn = 301;
		const int32 Here = NetWorld->GetRid();
		if (LookStage == 0)
		{
			if (HopFrom && (Here == HopFrom || Here == 0))
			{
				break;  // a hop under way
			}
			if (HopFrom)
			{
				HopFrom = 0;
				StepStart = Now;
			}
			if (Here == Inn)
			{
				LookStage = 10;
				StageTime = Now;
			}
			else if (Here && Now - StepStart > 1.0)
			{
				HopFrom = Here;
				if (!HopToward(Inn))
				{
					HopFrom = 0;
					LookStage = 10;  // no way there: look around here
					StageTime = Now;
				}
			}
			break;
		}
		if (LookStage == 10 && Now - StageTime > 1.5)
		{
			// every object here has an actor, drawn one way or another
			int32 ByLook = 0, ByBitmap = 0, ByProp = 0, None = 0;
			TArray<FString> Missing;
			for (const TPair<uint32, FMRNetObject>& Pair : Net->GetObjects())
			{
				if (Pair.Key == Me)
				{
					continue;
				}
				const AMRNetObject* A = NetWorld->FindActor(Pair.Key);
				if (!A)
				{
					Missing.Add(Pair.Value.Name);
				}
				else if (!A->GetLook().IsNone()) ++ByLook;
				else if (A->IsShownByProp()) ++ByProp;
				else if (!A->GetBgfName().IsEmpty()) ++ByBitmap;
				else ++None;
			}
			if (Missing.IsEmpty())
			{
				Pass(FString::Printf(TEXT("every object in %s has an actor: %d with our sprites, %d by props, %d from their bitmaps, %d without a picture"),
					*Net->GetPlayer().RoomName, ByLook, ByProp, ByBitmap, None));
			}
			else
			{
				Fail(FString::Printf(TEXT("look: objects without an actor: %s"), *FString::Join(Missing, TEXT(", "))));
			}
			// what we wear, as the server sends it (docs/adr/0012 M2b): our torso, arms and legs are converted parts
			if (const AMRCharacter* Pawn = Cast<AMRCharacter>(PC->GetPawn()))
			{
				const FMRSpriteAppearance& A = Pawn->GetSpriteAppearance();
				if (!A.BodyBgf.IsNone() && !A.LeftArmBgf.IsNone() && !A.RightArmBgf.IsNone() && !A.LegsBgf.IsNone())
				{
					Pass(FString::Printf(TEXT("we wear what the server says: %s"), *A.ToString()));
				}
				else
				{
					Fail(FString::Printf(TEXT("look: our worn parts didn't come through: %s"), *A.ToString()));
				}
			}
			// a named object that isn't a player: an NPC first
			const FMRNetObject* Pick = nullptr;
			for (const TPair<uint32, FMRNetObject>& Pair : Net->GetObjects())
			{
				const FMRNetObject& O = Pair.Value;
				if (O.IsPlayer() || !(O.Flags & MRMsg::OF_DISPLAY_NAME) || (O.Flags & MRMsg::OF_NOEXAMINE))
				{
					continue;
				}
				if (!Pick || ((O.Flags & MRMsg::OF_NPC) && !(Pick->Flags & MRMsg::OF_NPC)))
				{
					Pick = &O;
				}
			}
			LookId = Pick ? Pick->Id : Me;
			if (FApp::CanEverRender() && Pick)
			{
				// its name and the target's brackets over the world, before the dialog covers them
				NetWorld->SetTarget(LookId);
				if (const AMRNetObject* A = NetWorld->FindActor(LookId); A && PC->GetPawn())
				{
					PC->SetControlRotation(FRotator(-8.0, (A->GetActorLocation() - PC->GetPawn()->GetActorLocation()).Rotation().Yaw, 0.0));
				}
				LookStage = 11;
				StageTime = Now;
				break;
			}
			DescriptionsBefore = Descriptions;
			Net->RequestLook(LookId);
			LookStage = 1;
		}
		else if (LookStage == 11 && Now - StageTime > 1.0)
		{
			const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), TEXT("names.png"));
			FScreenshotRequest::RequestScreenshot(File, true, false);
			LookStage = 12;
			StageTime = Now;
		}
		else if (LookStage == 12 && Now - StageTime > 0.5)
		{
			DescriptionsBefore = Descriptions;
			Net->RequestLook(LookId);
			LookStage = 1;
		}
		else if (LookStage == 1 && Descriptions > DescriptionsBefore && D.Object.Id == LookId)
		{
			Pass(FString::Printf(TEXT("looked at %s: \"%s\"%s"), *D.Object.Name, *MRServerText::StripStyle(D.Text).Left(70),
				D.Inscription.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (inscribed \"%s\")"), *D.Inscription.Left(40))));
			StageTime = Now;
			LookStage = 2;
		}
		else if (LookStage == 2 && Now - StageTime > 1.5)
		{
			if (FApp::CanEverRender())
			{
				// (a moment after it opened: its picture downloads first)
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), TEXT("look_object.png")), true, false);
			}
			DescriptionsBefore = Descriptions;
			Net->RequestLook(Me);
			LookStage = 3;
		}
		else if (LookStage == 3 && Descriptions > DescriptionsBefore && D.Object.Id == Me)
		{
			if (D.bPlayer && D.bEditable)
			{
				Pass(FString::Printf(TEXT("looked at ourselves (UC_LOOK_PLAYER, editable): \"%s\""), *MRServerText::StripStyle(D.Text).Left(60)));
			}
			else
			{
				Fail(FString::Printf(TEXT("look: our own description came back as player %d, editable %d"), D.bPlayer, D.bEditable));
			}
			if (FApp::CanEverRender())
			{
				const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), TEXT("look_self.png"));
				PC->GetWorldTimerManager().SetTimerForNextTick([File]() { FScreenshotRequest::RequestScreenshot(File, true, false); });
			}
			Marker = FString::Printf(TEXT("An Unreal traveller (test %d)."), FMath::RandRange(100, 999));
			DescriptionsBefore = Descriptions;
			if (UI)
			{
				UI->SaveDescription(Me, Marker);  // as the dialog's Save does
			}
			else
			{
				Net->ChangeDescription(Me, Marker);
				Net->RequestLook(Me);
			}
			LookStage = 4;
		}
		else if (LookStage == 4 && Descriptions > DescriptionsBefore && D.Object.Id == Me)
		{
			if (D.Text.Contains(Marker))
			{
				Pass(FString::Printf(TEXT("changed our description (BP_CHANGE_DESCRIPTION) and read it back: \"%s\""), *Marker));
			}
			else
			{
				Fail(FString::Printf(TEXT("look: our new description didn't come back (\"%s\")"), *D.Text.Left(60)));
			}
			if (UI)
			{
				UI->CloseLook();
			}
			LookStage = 5;
			StageTime = Now;
			PropAimId = 0;
		}
		else if (LookStage == 5)
		{
			// an object drawn by a prop of the world build (the Inn's sign): turn to it; the
			// aim must take it, so Look can (its sight line used to end in the prop itself)
			const APawn* Pawn = PC->GetPawn();
			for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : NetWorld->GetActors())
			{
				const AMRNetObject* A = Pair.Value.Get();
				// the Inn's sign ("Welcome"), in the open (a stool behind the counter is really hidden)
				if (A && A->IsShownByProp() && A->GetObjectName() == TEXT("Welcome") && Pawn && PC->PlayerCameraManager)
				{
					const FVector Middle = (A->GetActorLocation() - FVector(0.0, 0.0, 80.0) + A->GetNameAnchor()) * 0.5;
					PC->SetControlRotation((Middle - PC->PlayerCameraManager->GetCameraLocation()).Rotation());
					PropAimId = Pair.Key;
					PropAimName = A->GetObjectName();
					break;
				}
			}
			if (!PropAimId || !FApp::CanEverRender())
			{
				Advance(EStep::Social);  // none near, or no view to aim in (headless): nothing to check
				break;
			}
			LookStage = 6;
			StageTime = Now;
		}
		else if (LookStage == 6 && Now - StageTime > 0.8)
		{
			if (NetWorld->GetAimId() == PropAimId || NetWorld->ObjectsAtAim().Contains(PropAimId))
			{
				Pass(FString::Printf(TEXT("aimed at the %s, drawn by a prop of the world build (so Look can take it)"), *PropAimName));
			}
			else
			{
				// what the sight line meets on the way (the camera to the object's middle)
				FString Seen;
				if (const AMRNetObject* A = NetWorld->FindActor(PropAimId); A && PC->PlayerCameraManager)
				{
					const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
					const FVector Middle = (A->GetActorLocation() - FVector(0.0, 0.0, 80.0) + A->GetNameAnchor()) * 0.5;
					FCollisionQueryParams Q(SCENE_QUERY_STAT(MRNetTestSight), false, PC->GetPawn());
					Q.AddIgnoredActor(A);
					TArray<FHitResult> Hits;
					PC->GetWorld()->LineTraceMultiByChannel(Hits, Cam, Middle, ECC_WorldStatic, Q);
					FHitResult H;
					PC->GetWorld()->LineTraceSingleByChannel(H, Cam, Middle, ECC_WorldStatic, Q);
					Seen = FString::Printf(TEXT("; actor at %s, middle %s, camera %s; first hit %s (tags %d) at %.0f cm"), *A->GetActorLocation().ToCompactString(),
						*Middle.ToCompactString(), *Cam.ToCompactString(), H.GetActor() ? *H.GetActor()->GetActorNameOrLabel() : TEXT("nothing"),
						H.GetActor() ? H.GetActor()->Tags.Num() : 0, H.Distance);
				}
				Fail(FString::Printf(TEXT("aim: the %s, drawn by a prop, can't be aimed at (aim on %u)%s"), *PropAimName, NetWorld->GetAimId(), *Seen));
			}
			Advance(EStep::Social);
		}
		break;
	}

	case EStep::Social:
	{
		// M8 (docs/research/blakserv-protocol.md "Chat and social"), in the Inn: its news board, a tell
		// and an emote through the chat line, mail to ourselves, the time, the options, the guild, a wave
		const FMRNetWorld& W = Net->GetNetWorld();
		UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
		const bool bRender = FApp::CanEverRender() && UI && UI->HasHUD();
		const FString Me = Net->GetSelf() ? Net->GetSelf()->Name : FString();
		auto Heard = [Net](const FString& Text)
		{
			return Net->GetChat().ContainsByPredicate([&Text](const FMRChatLine& L) { return L.Text.Contains(Text); });
		};
		auto Run = [UI](const FString& Line)
		{
			if (UI)
			{
				UI->RunChatLine(Line);  // as typed in the chat line
			}
		};
		const auto Shot = [](const TCHAR* Name)
		{
			const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), FString::Printf(TEXT("%s.png"), Name));
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
		};
		if (bTimedOut || !UI)
		{
			Fail(FString::Printf(TEXT("social: stuck at stage %d%s"), SocialStage, UI ? TEXT("") : TEXT(" (no UI)")));
			if (UI)
			{
				for (int32 i = 0; i < static_cast<int32>(EMRWindow::Count); ++i)
				{
					UI->SetWindowOpen(static_cast<EMRWindow>(i), false);
				}
			}
			Advance(EStep::Items);
			break;
		}
		const double Since = Now - SocialAt;
		// -Render: a picture of the window open now, once it has had a second to draw (true: wait)
		auto Picture = [&](int32 Index, const TCHAR* Name)
		{
			if (!bRender || SocialShot > Index)
			{
				return bRender && Since < 0.5;  // (a moment after the picture before closing)
			}
			if (Since > 1.0)
			{
				Shot(Name);
				SocialShot = Index + 1;
				SocialAt = Now;
			}
			return true;
		};
		switch (SocialStage)
		{
		case 0:
		{
			// the Inn's news board: looking at it opens it (BP_LOOK_NEWSGROUP), its articles follow
			const FMRNetObject* Board = nullptr;
			for (const TPair<uint32, FMRNetObject>& P : Net->GetObjects())
			{
				if (P.Value.Name.Contains(TEXT("News")))
				{
					Board = &P.Value;
				}
			}
			if (!Board)
			{
				Fail(TEXT("social: no news board in the Inn"));
				SocialStage = 2;
			}
			else
			{
				Net->RequestLook(Board->Id);
				SocialStage = 1;
			}
			SocialAt = Now;
			break;
		}
		case 1:
			if (W.News.bOpen && W.News.bHaveArticles && !W.News.bHaveText && W.News.Articles.Num() > 0 && W.News.ReadingNum == 0)
			{
				Net->ReadArticle(W.News.Articles.Last().Num);
			}
			else if (W.News.bOpen && W.News.bHaveArticles && (W.News.Articles.IsEmpty() || W.News.bHaveText))
			{
				if (Picture(0, TEXT("news")))
				{
					break;
				}
				Pass(FString::Printf(TEXT("read the %s board (BP_LOOK_NEWSGROUP, BP_ARTICLES, BP_ARTICLE): %d articles%s%s"), *W.News.Board.Name,
					W.News.Articles.Num(), W.News.Articles.Num() ? TEXT(", the last: ") : TEXT(""),
					W.News.Articles.Num() ? *FString::Printf(TEXT("\"%s\" (%d characters)"), *W.News.Articles.Last().Title, W.News.ReadingText.Len()) : TEXT("")));
				UI->SetWindowOpen(EMRWindow::News, false);
				Net->CloseNews();
				SocialStage = 2;
				SocialAt = Now;
			}
			break;
		case 2:
			// a tell to ourselves, typed: the name is found in the players list (BP_SAY_GROUP)
			SocialText = FString::Printf(TEXT("tell test %d"), FMath::RandRange(100, 999));
			Run(FString::Printf(TEXT("tell %s %s"), *Me, *SocialText));
			SocialStage = 3;
			SocialAt = Now;
			break;
		case 3:
			if (Heard(SocialText))
			{
				const FMRChatLine* L = Net->GetChat().FindByPredicate([this](const FMRChatLine& C) { return C.Text.Contains(SocialText); });
				Pass(FString::Printf(TEXT("typed \"tell %s ...\" and the tell came (BP_SAY_GROUP): \"%s\""), *Me, L ? *L->Text : TEXT("")));
				SocialText = FString::Printf(TEXT("smiles %d"), FMath::RandRange(100, 999));
				Run(TEXT(":") + SocialText);
				SocialStage = 4;
				SocialAt = Now;
			}
			else if (Since > 8.0)
			{
				Fail(FString::Printf(TEXT("social: our tell \"%s\" didn't come back"), *SocialText));
				SocialStage = 4;
				SocialText = FString::Printf(TEXT("smiles %d"), FMath::RandRange(100, 999));
				Run(TEXT(":") + SocialText);
			}
			break;
		case 4:
			if (Heard(SocialText) || Since > 8.0)
			{
				if (Heard(SocialText))
				{
					Pass(FString::Printf(TEXT("typed \":%s\" and the emote came (SAY_EMOTE)"), *SocialText));
				}
				else
				{
					Fail(FString::Printf(TEXT("social: the emote \"%s\" didn't come back"), *SocialText));
				}
				// mail to ourselves: the name looked up (BP_REQ_LOOKUP_NAMES), then sent (BP_SEND_MAIL)
				SocialText = FString::Printf(TEXT("Unreal mail %d"), FMath::RandRange(100, 999));
				MailResult = -1;
				Net->OnMailSent.AddWeakLambda(this, [this](bool bOk, const FString& Why) { MailResult = bOk ? 1 : 0; MailWhy = Why; });
				Net->SendMail({Me}, SocialText, TEXT("Sent from the Unreal client's test.\nA second line."));
				SocialStage = 5;
				SocialAt = Now;
			}
			break;
		case 5:
			if (MailResult == 0)
			{
				Fail(FString::Printf(TEXT("social: the mail wasn't sent: %s"), *MailWhy));
				SocialStage = 7;
			}
			else if (MailResult == 1)
			{
				Net->RequestMail();
				SocialStage = 6;
				SocialAt = Now;
			}
			break;
		case 6:
		{
			const int32 Index = Net->GetMail().IndexOfByPredicate([this](const FMRNetMail& M) { return M.Subject == SocialText; });
			if (Index != INDEX_NONE)
			{
				if (bRender && !UI->IsWindowOpen(EMRWindow::Mail))
				{
					UI->SetWindowOpen(EMRWindow::Mail, true);
					SocialAt = Now;
					break;
				}
				if (Picture(1, TEXT("mail")))
				{
					break;
				}
				const FMRNetMail& M = Net->GetMail()[Index];
				Pass(FString::Printf(TEXT("mailed ourselves (BP_REQ_LOOKUP_NAMES, BP_SEND_MAIL) and got it (BP_MAIL, kept, BP_DELETE_MAIL): \"%s\" from %s, %d lines"),
					*M.Subject, *M.From, M.Body.Len() ? 1 + M.Body.Replace(TEXT("\r"), TEXT("")).Len() - M.Body.Replace(TEXT("\n"), TEXT("")).Len() : 0));
				Net->DeleteMail(Index);
				UI->SetWindowOpen(EMRWindow::Mail, false);
				SocialStage = 7;
				SocialAt = Now;
			}
			else if (Since > 10.0)
			{
				Net->RequestMail();  // (mail can take a moment to arrive)
				SocialAt = Now;
			}
			break;
		}
		case 7:
			Net->OnMailSent.RemoveAll(this);
			Run(TEXT("/time"));
			SocialStage = 8;
			SocialAt = Now;
			break;
		case 8:
			if (!Heard(TEXT("The time in Meridian is")) && Since > 5.0)
			{
				Run(TEXT("/time"));  // (asked again: a request while the server saves is dropped)
				SocialAt = Now;
			}
			else if (Heard(TEXT("The time in Meridian is")))
			{
				const FMRChatLine* L = Net->GetChat().FindByPredicate([](const FMRChatLine& C) { return C.Text.Contains(TEXT("The time in Meridian is")); });
				Pass(FString::Printf(TEXT("typed /time (UC_REQ_TIME): \"%s\""), L ? *L->Text : TEXT("")));
				SocialStage = 9;
				SocialAt = Now;
			}
			break;
		case 9:
			// the server-kept options: "spellpower on" sets CF_SPELLPOWER (UC_SEND_PREFERENCES); asked again, the server agrees
			if (Net->HasPreferences())
			{
				PrefsBefore = Net->GetPreferences();
				Run((PrefsBefore & MRMsg::CF_SPELLPOWER) ? TEXT("/spellpower off") : TEXT("/spellpower on"));
				Net->RequestPreferences();
				SocialStage = 10;
				SocialAt = Now;
			}
			break;
		case 10:
			if (Since > 1.5 && Net->HasPreferences())
			{
				const bool bFlipped = (Net->GetPreferences() ^ PrefsBefore) == MRMsg::CF_SPELLPOWER;
				if (bFlipped)
				{
					Pass(FString::Printf(TEXT("the server's options (UC_RECEIVE_PREFERENCES %x): typed /spellpower and it kept %x"), PrefsBefore, Net->GetPreferences()));
				}
				else
				{
					Fail(FString::Printf(TEXT("social: options %x, then %x after /spellpower"), PrefsBefore, Net->GetPreferences()));
				}
				Net->SetPreferences(PrefsBefore);
				Run(TEXT("/guild"));
				SocialStage = 11;
				SocialAt = Now;
			}
			break;
		case 11:
			if (Heard(TEXT("You do not belong to a guild")) || W.Guild.bValid)
			{
				if (bRender && !W.Guild.bValid && SocialShot == 2)
				{
					// the window with a guild to show (the test character has none)
					FMRNetGuild G;
					G.bValid = true;
					G.Name = TEXT("The Unreal Guild");
					G.Flags = MRMsg::GC_INVITE | MRMsg::GC_EXILE | MRMsg::GC_SET_RANK | MRMsg::GC_RENOUNCE;
					const TCHAR* Ranks[5] = {TEXT("Initiate"), TEXT("Member"), TEXT("Veteran"), TEXT("Lieutenant"), TEXT("Guildmaster")};
					for (int32 i = 0; i < 5; ++i)
					{
						G.MaleRanks[i] = G.FemaleRanks[i] = Ranks[i];
					}
					G.Members = {{1, Me, 5, 1}, {2, TEXT("Ann"), 3, 2}, {3, TEXT("Bob"), 1, 1}};
					Net->DebugSetGuild(G, FMRNetGuildList());
					UI->SetWindowOpen(EMRWindow::Guild, true);
					SocialAt = Now;
					break;
				}
				if (Picture(2, TEXT("guild")))
				{
					break;
				}
				if (bRender)
				{
					Net->DebugSetGuild(FMRNetGuild(), FMRNetGuildList());
				}
				UI->SetWindowOpen(EMRWindow::Guild, false);
				Pass(W.Guild.bValid ? FString::Printf(TEXT("typed /guild: %s"), *W.Guild.Name) : FString(TEXT("typed /guild (UC_REQ_GUILDINFO): \"You do not belong to a guild.\"")));
				Run(TEXT("/wave"));
				SocialStage = 12;
				SocialAt = Now;
			}
			break;
		case 12:
			if (Heard(TEXT("You wave your hand")))
			{
				Pass(TEXT("typed /wave (BP_ACTION UA_WAVE): \"You wave your hand.\""));
				if (bRender)
				{
					UI->SetWindowOpen(EMRWindow::Who, true);
				}
				SocialStage = 13;
				SocialAt = Now;
			}
			break;
		default:
			if (Picture(3, TEXT("who")))
			{
				break;
			}
			UI->SetWindowOpen(EMRWindow::Who, false);
			Advance(EStep::Settings);
			break;
		}
		break;
	}

	case EStep::Settings:
	{
		// M9: the Options pages, the large map and a note, the quick chat, the password (changed and
		// back), deleting a character (refused so soon: only -Create), and a hotbar kept for the relog
		UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
		const bool bRender = FApp::CanEverRender() && UI && UI->HasHUD();
		const double Since = Now - SocialAt;
		const auto Shot = [](const TCHAR* Name)
		{
			const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), FString::Printf(TEXT("%s.png"), Name));
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
		};
		auto Count = [Net](const TCHAR* Text)
		{
			int32 N = 0;
			for (const FMRChatLine& L : Net->GetChat())
			{
				N += L.Text.Contains(Text) ? 1 : 0;
			}
			return N;
		};
		if (bTimedOut || !UI)
		{
			Fail(FString::Printf(TEXT("settings: stuck at stage %d"), SettingsStage));
			if (UI)
			{
				UI->SetWindowOpen(EMRWindow::Options, false);
				UI->SetWindowOpen(EMRWindow::Map, false);
			}
			Advance(EStep::Items);
			break;
		}
		if (SettingsStage == 0)
		{
			Net->OnPasswordChanged.AddWeakLambda(this, [this](bool bOk) { ++PasswordAnswers; bLastPasswordOk = bOk; });
			SettingsStage = bRender ? 1 : 10;
			SocialAt = Now;
			break;
		}
		// -Render: the Options pages and the map, a second each
		const TCHAR* Pages[] = {TEXT("Graphics"), TEXT("Controls"), TEXT("Account")};
		if (SettingsStage >= 1 && SettingsStage <= 3)
		{
			if (Since < 0.2)
			{
				break;
			}
			if (!UI->IsWindowOpen(EMRWindow::Options) || StaticCastSharedPtr<SMROptionsDialog>(UI->GetWindow(EMRWindow::Options))->GetTab() != Pages[SettingsStage - 1])
			{
				UI->ShowOptions(Pages[SettingsStage - 1]);
				SocialAt = Now;
				break;
			}
			if (Since > 1.0)
			{
				Shot(*FString::Printf(TEXT("options_%s"), *FString(Pages[SettingsStage - 1]).ToLower()));
				++SettingsStage;
				SocialAt = Now;
			}
			break;
		}
		if (SettingsStage == 4)
		{
			UI->SetWindowOpen(EMRWindow::Options, false);
			SettingsStage = 10;
			SocialAt = Now;
			break;
		}
		switch (SettingsStage)
		{
		case 10:
		{
			// the large map, and a note where we stand (kept per character and room)
			UI->SetWindowOpen(EMRWindow::Map, true);
			MapNote = FString::Printf(TEXT("note %d"), FMath::RandRange(100, 999));
			if (TSharedPtr<SMRSocialWindow> W = UI->GetWindow(EMRWindow::Map))
			{
				StaticCastSharedPtr<SMRMapWindow>(W)->AddNote(MapNote);
			}
			else
			{
				// (no HUD: as the window would)
				FMRSocial& S = Net->GetSocial();
				S.MapNotes.FindOrAdd(Net->GetPlayer().RoomFile.ToLower()).Add({0.0, 0.0, MapNote});
				Net->SaveSocial();
			}
			SettingsStage = 11;
			SocialAt = Now;
			break;
		}
		case 11:
		{
			if (bRender && Since < 1.2)
			{
				break;
			}
			if (bRender)
			{
				Shot(TEXT("map"));
			}
			TArray<FMRMapNote>* Notes = Net->GetSocial().MapNotes.Find(Net->GetPlayer().RoomFile.ToLower());
			const int32 At = Notes ? Notes->IndexOfByPredicate([this](const FMRMapNote& N) { return N.Text == MapNote; }) : INDEX_NONE;
			if (At != INDEX_NONE)
			{
				Pass(FString::Printf(TEXT("a note on the large map of %s (\"%s\"), kept with the character"), *Net->GetPlayer().RoomName, *MapNote));
			}
			else
			{
				Fail(TEXT("settings: the map note wasn't kept"));
			}
			SettingsStage = 12;
			SocialAt = Now;
			break;
		}
		case 12:
			if (Since < 1.5)
			{
				break;  // (a gesture counts as an attack: one a second, and /wave was just now)
			}
			UI->SetWindowOpen(EMRWindow::Map, false);
			if (TArray<FMRMapNote>* Notes = Net->GetSocial().MapNotes.Find(Net->GetPlayer().RoomFile.ToLower()))
			{
				Notes->RemoveAll([this](const FMRMapNote& N) { return N.Text == MapNote; });  // (tidy, after the picture)
				Net->SaveSocial();
			}
			// the quick chat: F8 is "wave" (the original's default)
			WavesBefore = Count(TEXT("You wave your hand"));
			UI->RunQuickChat(7);
			SettingsStage = 13;
			SocialAt = Now;
			break;
		case 13:
			if (Count(TEXT("You wave your hand")) > WavesBefore)
			{
				Pass(FString::Printf(TEXT("F8, the quick chat's \"%s\", waved"), *Net->GetSocial().QuickChat[7]));
				SettingsStage = 14;
				SocialAt = Now;
			}
			else if (Since > 5.0)
			{
				Fail(FString::Printf(TEXT("settings: F8's \"%s\" didn't wave"), *Net->GetSocial().QuickChat[7]));
				SettingsStage = 14;
				SocialAt = Now;
			}
			break;
		case 14:
		{
			// the password: a wrong old one is refused; the right one changes it, and back again
			if (Since < 1.0)
			{
				break;  // (one attack time after the wave)
			}
			PasswordAnswers = 0;
			const FString Why = Net->ChangePassword(TestPassword + TEXT("-wrong"), TestPassword + TEXT("-new"));
			if (!Why.IsEmpty())
			{
				Fail(TEXT("settings: ") + Why);
				SettingsStage = 18;
			}
			else
			{
				SettingsStage = 15;
			}
			SocialAt = Now;
			break;
		}
		case 15:
			if (PasswordAnswers == 1)
			{
				if (bLastPasswordOk)
				{
					Fail(TEXT("settings: the server took a wrong old password"));
					SettingsStage = 16;  // (changed: change it back below)
				}
				else
				{
					PasswordAnswers = 0;
					Net->ChangePassword(TestPassword, TestPassword + TEXT("-new"));
					SettingsStage = 16;
				}
				SocialAt = Now;
			}
			break;
		case 16:
			if (PasswordAnswers >= 1)
			{
				const bool bChanged = bLastPasswordOk;
				PasswordAnswers = 0;
				Net->ChangePassword(TestPassword + TEXT("-new"), TestPassword);
				SettingsStage = bChanged ? 17 : 18;
				if (!bChanged)
				{
					Fail(TEXT("settings: the password change was refused"));
				}
				SocialAt = Now;
			}
			break;
		case 17:
			if (PasswordAnswers >= 1)
			{
				if (bLastPasswordOk)
				{
					Pass(TEXT("the password (BP_CHANGE_PASSWORD): a wrong old one refused (BP_PASSWORD_NOT_OK), changed and changed back (BP_PASSWORD_OK)"));
				}
				else
				{
					Fail(FString::Printf(TEXT("settings: the password couldn't be changed back: it is now \"%s-new\""), *TestPassword));
				}
				SettingsStage = 18;
				SocialAt = Now;
			}
			break;
		case 18:
			SettingsStage = 20;  // (deleting the character: the Delete step, at the end of a -Create run)
			break;
		case 20:
		{
			// a carried item on the hotbar's last slot: kept with the character, for the relog to find
			// (a stack: shillings or reagents, which nothing wields later)
			UMRInventorySource* Source = UI->GetSource();
			int32 Pick = INDEX_NONE;
			for (int32 i = 0; Source && i < 40 && Pick == INDEX_NONE; ++i)
			{
				const FMRSlotContent C = Source->Get(FMRSlotRef(EMRSlotArea::Bag, i));
				const FMRNetObject* O = C.ObjectId ? Net->FindInventory(C.ObjectId) : nullptr;
				Pick = O && O->bNumber ? i : INDEX_NONE;
			}
			if (Source && Pick != INDEX_NONE)
			{
				Source->SwapWithHotbar(FMRSlotRef(EMRSlotArea::Bag, Pick), 8);
			}
			const FMRSocial& S = Net->GetSocial();
			HotbarKey = S.Hotbar.IsValidIndex(8) ? S.Hotbar[8] : FString();
			if (HotbarKey.IsEmpty())
			{
				Fail(TEXT("settings: nothing kept on the hotbar's last slot"));
			}
			Net->OnPasswordChanged.RemoveAll(this);
			Advance(PalName.IsEmpty() ? EStep::Items : EStep::Pair);
			break;
		}
		default:
			break;
		}
		break;
	}

	case EStep::Pair:
	{
		// a second player (tools/ue/second_player.ts, run_net_test.ps1 -Pair): who's on, tells both ways,
		// ignoring and a blocked tell, broadcasts and turning them off, its wave, and a trade between players.
		// It obeys our tells that start "pal:".
		UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
		const double Since = Now - SocialAt;
		// a line the second player said (not our own echo of the order, "You tell Unrealpal, ...")
		auto HeardFrom = [this, Net](const FString& Text, uint8 Kind)
		{
			return Net->GetChat().ContainsByPredicate([&](const FMRChatLine& L)
			{
				return L.Kind == Kind && L.Text.StartsWith(PalName) && L.Text.Contains(Text);
			});
		};
		auto Order = [this, Net, UI](const FString& What)
		{
			// as typed: "tell <name> pal: ..."
			if (UI)
			{
				UI->RunChatLine(FString::Printf(TEXT("tell %s pal: %s"), *PalName, *What));
			}
			else
			{
				Net->SayTo({PalId}, TEXT("pal: ") + What);
			}
		};
		auto Coins = [Net]()
		{
			uint32 N = 0;
			for (const FMRNetObject& O : Net->GetInventory())
			{
				N += O.Icon.Equals(TEXT("coin.bgf"), ESearchCase::IgnoreCase) ? O.Amount : 0;
			}
			return N;
		};
		if (bTimedOut)
		{
			Fail(FString::Printf(TEXT("pair: stuck at stage %d"), PairStage));
			Advance(EStep::Items);
			break;
		}
		switch (PairStage)
		{
		case 0:
			for (const TPair<uint32, FMRNetUser>& U : Net->GetUsers())
			{
				if (U.Value.Name.Equals(PalName, ESearchCase::IgnoreCase))
				{
					PalId = U.Key;
				}
			}
			if (PalId)
			{
				Pass(FString::Printf(TEXT("the players list has a second player: %s (%d on)"), *PalName, Net->GetUsers().Num()));
				PairText = FString::Printf(TEXT("hello %d"), FMath::RandRange(100, 999));
				Order(TEXT("tell ") + PairText);
				PairStage = 1;
				SocialAt = Now;
			}
			else if (Since > 20.0)
			{
				Fail(FString::Printf(TEXT("pair: %s never logged on"), *PalName));
				Advance(EStep::Items);
			}
			break;
		case 1:
			if (HeardFrom(PairText, MRMsg::SAY_GROUP))
			{
				Pass(FString::Printf(TEXT("told %s and was told back (BP_SAY_GROUP): \"%s\""), *PalName, *PairText));
				// ignored: its tell never shows, and the server hears it was blocked (BP_SAY_BLOCKED)
				Net->SetIgnored(PalName, true);
				PairText = FString::Printf(TEXT("secret %d"), FMath::RandRange(100, 999));
				Order(TEXT("tell ") + PairText);
				PairStage = 2;
				SocialAt = Now;
			}
			else if (Since > 8.0)
			{
				Fail(FString::Printf(TEXT("pair: no tell came back from %s"), *PalName));
				PairStage = 2;
				SocialAt = Now;
			}
			break;
		case 2:
			if (Since > 4.0)
			{
				if (HeardFrom(PairText, MRMsg::SAY_GROUP))
				{
					Fail(TEXT("pair: an ignored player's tell showed"));
				}
				else
				{
					Pass(FString::Printf(TEXT("ignored %s: its tell \"%s\" didn't show (BP_SAY_BLOCKED went back)"), *PalName, *PairText));
				}
				Net->SetIgnored(PalName, false);
				PairText = FString::Printf(TEXT("news %d"), FMath::RandRange(100, 999));
				Order(TEXT("broadcast ") + PairText);
				PairStage = 3;
				SocialAt = Now;
			}
			break;
		case 3:
			if (HeardFrom(PairText, MRMsg::SAY_EVERYONE))
			{
				Pass(FString::Printf(TEXT("heard %s's broadcast (SAY_EVERYONE): \"%s\""), *PalName, *PairText));
				Net->GetSocial().bNoBroadcast = true;
				PairText = FString::Printf(TEXT("quiet %d"), FMath::RandRange(100, 999));
				Order(TEXT("broadcast ") + PairText);
				PairStage = 4;
				SocialAt = Now;
			}
			else if (Since > 8.0)
			{
				Fail(FString::Printf(TEXT("pair: %s's broadcast didn't come"), *PalName));
				PairStage = 5;
				SocialAt = Now;
			}
			break;
		case 4:
			if (Since > 4.0)
			{
				if (HeardFrom(PairText, MRMsg::SAY_EVERYONE))
				{
					Fail(TEXT("pair: a broadcast showed with broadcasts off"));
				}
				else
				{
					Pass(TEXT("with broadcasts off, the next one didn't show"));
				}
				Net->GetSocial().bNoBroadcast = false;
				Net->SaveSocial();
				PairStage = 5;
				SocialAt = Now;
			}
			break;
		case 5:
		{
			// in the same room (the Inn): its wave on its sprite, and a trade
			const FMRNetObject* Pal = Net->FindObject(PalId);
			if (!Pal)
			{
				UE_LOG(LogMeridian, Display, TEXT("MRNetTest: %s isn't in %s: no wave or trade to try"), *PalName, *Net->GetPlayer().RoomName);
				PairStage = 9;
				break;
			}
			// its wave comes as a BP_CHANGE with a one-off animation on its arm (player.kod DoWave)
			bPalAnimated = false;
			Net->OnObjectChanged.AddWeakLambda(this, [this, Net](uint32 Id)
			{
				const FMRNetObject* O = Id == PalId ? Net->FindObject(Id) : nullptr;
				if (O && (O->Animation.Type == MRMsg::ANIMATE_ONCE
					|| O->OverlayParts.ContainsByPredicate([](const FMRNetOverlay& Ov) { return Ov.Animation.Type == MRMsg::ANIMATE_ONCE; })))
				{
					bPalAnimated = true;
				}
			});
			Order(TEXT("wave"));
			PairStage = 6;
			SocialAt = Now;
			break;
		}
		case 6:
		{
			const AMRNetObject* A = NetWorld->FindActor(PalId);
			const FName Action = A && A->GetSpriteBody() ? A->GetSpriteBody()->GetAction() : NAME_None;
			if (bPalAnimated)
			{
				Pass(FString::Printf(TEXT("%s waved (BP_ACTION; its BP_CHANGE brought a one-off animation%s)"), *PalName,
					Action.IsNone() ? TEXT("") : *FString::Printf(TEXT(", its sprite plays %s"), *Action.ToString())));
			}
			else if (Since < 5.0)
			{
				break;
			}
			else
			{
				Fail(FString::Printf(TEXT("pair: no wave came from %s"), *PalName));
			}
			Net->OnObjectChanged.RemoveAll(this);
			CoinsBeforeTrade = Coins();
			Order(TEXT("offer"));
			PairStage = 7;
			SocialAt = Now;
			break;
		}
		case 7:
		{
			// it offers us a shilling (BP_OFFER); we answer with nothing; it accepts
			const FMRNetTrade& T = Net->GetTrade();
			if (T.bOpen && !T.bOurs && T.WithId == PalId && !T.bCountered)
			{
				Net->Counteroffer({});
				PairStage = 8;
				SocialAt = Now;
			}
			else if (Since > 8.0)
			{
				Fail(FString::Printf(TEXT("pair: no offer came from %s"), *PalName));
				PairStage = 9;
			}
			break;
		}
		case 8:
			if (Coins() > CoinsBeforeTrade && !Net->GetTrade().bOpen)
			{
				Pass(FString::Printf(TEXT("traded with %s (BP_OFFER, BP_REQ_COUNTEROFFER, its accept): %u shillings, from %u"), *PalName,
					Coins(), CoinsBeforeTrade));
				PairStage = 9;
			}
			else if (Since > 8.0)
			{
				Fail(FString::Printf(TEXT("pair: the trade with %s didn't finish (open %d, shillings %u)"), *PalName, Net->GetTrade().bOpen, Coins()));
				Net->CancelOffer();
				PairStage = 9;
			}
			break;
		default:
			Order(TEXT("quit"));
			Advance(EStep::Items);
			break;
		}
		break;
	}

	case EStep::Items:
	{
		const FMRNetWorld& W = Net->GetNetWorld();
		UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
		if (bTimedOut)
		{
			Fail(FString::Printf(TEXT("items: stuck at stage %d (%s)"), ItemStage, *ItemName));
			Advance(EStep::Travel);
			break;
		}
		const FMRNetObject* Item = ItemId ? Net->FindInventory(ItemId) : nullptr;
		if (ItemStage == 0 && W.bHasInventory && Now - StepStart > 1.0)
		{
			TArray<FString> Lines;
			for (const FMRNetObject& O : W.Inventory)
			{
				Lines.Add(FString::Printf(TEXT("%s%s (%s)%s"), O.bNumber ? *FString::Printf(TEXT("%u "), O.Amount) : TEXT(""), *O.Name,
					*O.Icon, Net->IsUsing(O.Id) ? TEXT(", in use") : TEXT("")));
			}
			Pass(FString::Printf(TEXT("carrying %d items: %s"), W.Inventory.Num(), *FString::Join(Lines, TEXT("; "))));
			// the inventory screen's source shows them: in use on equipment slots, the rest in the bag
			if (UMRInventorySource* Src = UI ? UI->GetSource() : nullptr)
			{
				int32 Shown = 0, Worn = 0;
				for (int32 i = 0; i < Src->NumSlots(EMRSlotArea::Bag); ++i)
				{
					Shown += Src->Get(FMRSlotRef(EMRSlotArea::Bag, i)).ObjectId ? 1 : 0;
				}
				for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::Count); ++i)
				{
					Worn += Src->Get(FMRSlotRef::Equip(static_cast<EMREquipSlot>(i))).ObjectId ? 1 : 0;
				}
				for (int32 i = 0; i < 9; ++i)
				{
					Shown += Src->Get(FMRSlotRef(EMRSlotArea::Hotbar, i)).ObjectId ? 1 : 0;
				}
				if (Shown + Worn == W.Inventory.Num())
				{
					Pass(FString::Printf(TEXT("the inventory screen shows them: %d on equipment slots, %d in the bag"), Worn, Shown));
				}
				else
				{
					Fail(FString::Printf(TEXT("items: the inventory screen shows %d of %d"), Shown + Worn, W.Inventory.Num()));
				}
			}
			// the item to try: something not counted (a weapon, clothes), in use or not
			const FMRNetObject* Pick = W.Inventory.FindByPredicate([&](const FMRNetObject& O) { return !O.bNumber && Net->IsUsing(O.Id); });
			Pick = Pick ? Pick : W.Inventory.FindByPredicate([](const FMRNetObject& O) { return !O.bNumber; });
			if (!Pick)
			{
				Fail(TEXT("items: we carry nothing to try"));
				Advance(EStep::Travel);
				break;
			}
			ItemId = Pick->Id;
			ItemName = Pick->Name;
			bItemWasInUse = Net->IsUsing(ItemId);
			if (bItemWasInUse)
			{
				Net->UnuseItem(ItemId);
			}
			else
			{
				Net->UseItem(ItemId);
			}
			ItemStage = 1;
			StageTime = Now;
		}
		else if (ItemStage == 1 && Net->IsUsing(ItemId) != bItemWasInUse)
		{
			// in use: on its equipment slot in the inventory screen
			FString Where;
			if (UMRInventorySource* Src = UI ? UI->GetSource() : nullptr)
			{
				for (int32 i = 0; i < static_cast<int32>(EMREquipSlot::Count); ++i)
				{
					if (Src->Get(FMRSlotRef::Equip(static_cast<EMREquipSlot>(i))).ObjectId == ItemId)
					{
						Where = FString::Printf(TEXT(", shown on equipment slot %d"), i);
					}
				}
			}
			Pass(FString::Printf(TEXT("%s %s (%s)%s"), bItemWasInUse ? TEXT("took off") : TEXT("used"), *ItemName,
				bItemWasInUse ? TEXT("BP_REQ_UNUSE, BP_UNUSE") : TEXT("BP_REQ_USE, BP_USE"), *Where));
			if (FApp::CanEverRender() && UI && !bItemWasInUse)
			{
				// the inventory screen with it in hand (Saved/Screenshots/MRNet/inventory.png)
				UI->SetInventoryOpen(true);
				const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), TEXT("inventory.png"));
				FTimerHandle T;
				PC->GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateLambda([File]() { FScreenshotRequest::RequestScreenshot(File, true, false); }), 1.0f, false);
				FTimerHandle T2;
				TWeakObjectPtr<UMRUISubsystem> WeakUI(UI);
				PC->GetWorldTimerManager().SetTimer(T2, FTimerDelegate::CreateLambda([WeakUI]() { if (WeakUI.IsValid()) WeakUI->SetInventoryOpen(false); }), 2.0f, false);
				StageTime = Now;
			}
			ItemStage = 10;  // (a moment for the picture), then back as it was
		}
		else if (ItemStage == 10 && Now - StageTime > 2.5)
		{
			if (bItemWasInUse)
			{
				Net->UseItem(ItemId);
			}
			else
			{
				Net->UnuseItem(ItemId);
			}
			ItemStage = 2;
		}
		else if (ItemStage == 2 && Net->IsUsing(ItemId) == bItemWasInUse)
		{
			Pass(FString::Printf(TEXT("%s %s again"), bItemWasInUse ? TEXT("put on") : TEXT("put away"), *ItemName));
			if (bItemWasInUse)
			{
				Net->UnuseItem(ItemId);  // (taken off to be dropped)
			}
			ItemStage = 3;
		}
		else if (ItemStage == 3 && Item && !Net->IsUsing(ItemId))
		{
			Net->Drop(ItemId);
			ItemStage = 4;
		}
		else if (ItemStage == 4 && !Item)
		{
			// it lies in the room now: an object of its name that we don't carry
			for (const TPair<uint32, FMRNetObject>& Pair : Net->GetObjects())
			{
				if (Pair.Value.Name == ItemName && (Pair.Value.Flags & MRMsg::OF_GETTABLE))
				{
					DroppedId = Pair.Key;
				}
			}
			if (DroppedId)
			{
				Pass(FString::Printf(TEXT("dropped %s (BP_REQ_DROP): it left the inventory and lies in the room"), *ItemName));
				Net->Pickup(DroppedId);
				ItemStage = 5;
			}
		}
		else if (ItemStage == 5)
		{
			const FMRNetObject* Back = W.Inventory.FindByPredicate([&](const FMRNetObject& O) { return O.Name == ItemName; });
			if (Back && !Net->FindObject(DroppedId))
			{
				Pass(FString::Printf(TEXT("picked %s up again (BP_REQ_GET)"), *ItemName));
				ItemId = Back->Id;
				if (bItemWasInUse)
				{
					Net->UseItem(ItemId);  // as it was
				}
				// then some of a number item (the tagged id and an amount)
				const FMRNetObject* Coins = W.Inventory.FindByPredicate([](const FMRNetObject& O) { return O.bNumber && O.Amount > 10; });
				if (Coins)
				{
					ItemId = Coins->Id;
					ItemName = Coins->Name;
					CoinsBefore = Coins->Amount;
					Net->Drop(ItemId, 10);
					DroppedId = 0;
					ItemStage = 7;
				}
				else
				{
					ItemStage = 6;
				}
				StageTime = Now;
			}
		}
		else if (ItemStage == 7 && Item && Item->Amount == CoinsBefore - 10)
		{
			for (const TPair<uint32, FMRNetObject>& Pair : Net->GetObjects())
			{
				if (Pair.Value.bNumber && Pair.Value.Amount == 10 && Pair.Value.Name == ItemName)
				{
					DroppedId = Pair.Key;
				}
			}
			if (DroppedId)
			{
				Pass(FString::Printf(TEXT("dropped 10 of %u %s (a number item: tagged id and amount); 10 lie in the room"), CoinsBefore, *ItemName));
				Net->Pickup(DroppedId);
				ItemStage = 8;
			}
		}
		else if (ItemStage == 8 && Item && Item->Amount == CoinsBefore && !Net->FindObject(DroppedId))
		{
			Pass(FString::Printf(TEXT("picked the 10 %s up: %u again"), *ItemName, CoinsBefore));
			ItemStage = 6;
			StageTime = Now;
		}
		else if (ItemStage == 6 && Now - StageTime > 1.0)
		{
			Advance(EStep::Spells);
		}
		break;
	}

	case EStep::Spells:
	{
		const FMRNetWorld& W = Net->GetNetWorld();
		UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
		if (bTimedOut)
		{
			Fail(FString::Printf(TEXT("spells: stuck at stage %d"), SpellStage));
			NetWorld->SetResting(false);
			Advance(EStep::Combat);
			break;
		}
		const FMRNetSpell* Appraise = W.Spells.FindByPredicate([](const FMRNetSpell& S) { return S.Object.Name == TEXT("appraise"); });
		const FMRNetSpell* Meditate = W.Spells.FindByPredicate([](const FMRNetSpell& S) { return S.Object.Name == TEXT("meditate"); });
		// the server's line after a cast (not appraise's: it waits for a value to be said, and the next cast breaks it off)
		const auto Answer = [&](bool bSkipAppraise) -> FString
		{
			const TArray<FMRChatLine>& Chat = Net->GetChat();
			for (int32 i = Chat.Num() - 1; i >= SpellChat && i >= 0; --i)
			{
				if (!bSkipAppraise || !Chat[i].Text.Contains(TEXT("appraise")))
				{
					return Chat[i].Text;
				}
			}
			return FString();
		};
		if (SpellStage == 0 && W.bHasSpells && W.bHasSkills && Now - StepStart > 1.0)
		{
			TArray<FString> Names;
			for (const FMRNetSpell& S : W.Spells)
			{
				Names.Add(FString::Printf(TEXT("%s%s"), *S.Object.Name, S.Targets ? TEXT(" (1 target)") : TEXT("")));
			}
			const int32 Shown = UI && UI->GetSource() ? UI->GetSource()->GetKnownSpells().Num() : 0;
			if (Shown == W.Spells.Num())
			{
				Pass(FString::Printf(TEXT("the server's %d spells (BP_SPELLS) are on the Spells page: %s; %d skills (BP_SKILLS)"),
					W.Spells.Num(), *FString::Join(Names, TEXT(", ")), W.Skills.Num()));
			}
			else
			{
				Fail(FString::Printf(TEXT("spells: the Spells page has %d of the server's %d"), Shown, W.Spells.Num()));
			}
			TArray<FString> Room;
			for (const FMRNetObject& E : Net->GetRoomEnchantments())
			{
				Room.Add(E.Name);
			}
			if (Room.Num() > 0)
			{
				Pass(FString::Printf(TEXT("the room's enchantments (BP_ADD_ENCHANTMENT): %s"), *FString::Join(Room, TEXT(", "))));
			}
			else
			{
				Fail(FString::Printf(TEXT("spells: no enchantment on %s (the Inn is a safe room)"), *Net->GetPlayer().RoomName));
			}
			SpellStage = 1;
		}
		else if (SpellStage == 1 && UI && FApp::CanEverRender() && SpellShot < 4)
		{
			// -Render: the Quests page, then the hint while an item waits for its target
			if (Now - StageTime < 0.8)
			{
				break;
			}
			const FMRNetObject* Mace = W.Inventory.FindByPredicate([](const FMRNetObject& O) { return O.Icon.Equals(TEXT("mace.bgf"), ESearchCase::IgnoreCase); });
			const auto Shot = [](const TCHAR* Name)
			{
				const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), FString::Printf(TEXT("%s.png"), Name));
				FScreenshotRequest::RequestScreenshot(File, true, false);
				UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
			};
			switch (SpellShot++)
			{
			case 0: UI->SetInventoryOpen(true); UI->SetInventoryTab(4); break;
			case 1: Shot(TEXT("quests")); break;
			case 2: UI->SetInventoryTab(0); UI->SetInventoryOpen(false); if (Mace) { NetWorld->ApplyItem(Mace->Id); } break;
			default: Shot(TEXT("choose_target")); break;
			}
			StageTime = Now;
		}
		else if (SpellStage == 1 && UI)
		{
			NetWorld->CancelChoosing();
			TArray<FMRQuestView> Quests;
			UI->GetQuests(Quests);
			if (Quests.Num() > 0)
			{
				TArray<FString> Lines;
				for (const FMRQuestView& Q : Quests)
				{
					Lines.Add(Q.ObjectId ? Q.Name : FString::Printf(TEXT("[%s]"), *Q.Name));
				}
				Pass(FString::Printf(TEXT("the Quests page lists the server's quest group: %s"), *FString::Join(Lines, TEXT(" "))));
				// appraise the mace: no target, nothing at the crosshair (-nullrhi), so the choice picks it
				const FMRNetObject* Mace = W.Inventory.FindByPredicate([](const FMRNetObject& O) { return O.Icon.Equals(TEXT("mace.bgf"), ESearchCase::IgnoreCase); });
				if (!Appraise || !Mace)
				{
					Fail(TEXT("spells: no appraise spell or no mace to cast it on"));
					Advance(EStep::Combat);
					break;
				}
				NetWorld->ClearTarget();
				SpellChat = Net->GetChat().Num();
				const bool bSent = NetWorld->CastSpell(Appraise->Object.Id);
				if (!bSent && NetWorld->IsChoosingTarget())
				{
					UE_LOG(LogMeridian, Display, TEXT("MRNetTest: %s"), *NetWorld->GetChoosingText());
					NetWorld->ChooseTarget(Mace->Id);
				}
				SpellStage = 2;
				StageTime = Now;
			}
		}
		else if (SpellStage == 2 && !Answer(false).IsEmpty() && Now - StageTime > 0.5)
		{
			Pass(FString::Printf(TEXT("cast appraise on the mace (BP_REQ_CAST, one target): \"%s\""), *Answer(false)));
			SpellChat = Net->GetChat().Num();
			NetWorld->CastSpell(Meditate ? Meditate->Object.Id : 0);
			SpellStage = 3;
			StageTime = Now;
		}
		else if (SpellStage == 3 && Answer(true).IsEmpty() && Now - StageTime > 1.5)
		{
			// the first cast only broke appraise's trance (user.kod UserCast: a cast in a trance ends it): again
			SpellChat = Net->GetChat().Num();
			NetWorld->CastSpell(Meditate ? Meditate->Object.Id : 0);
			SpellStage = 5;
			StageTime = Now;
		}
		else if ((SpellStage == 3 || SpellStage == 5) && !Answer(true).IsEmpty() && Now - StageTime > 0.5)
		{
			Pass(FString::Printf(TEXT("cast meditate (no target): \"%s\""), *Answer(true)));
			NetWorld->SetResting(true);
			SpellStage = 4;
			StageTime = Now;
		}
		else if (SpellStage == 4 && Now - StageTime > 1.0)
		{
			const ACharacter* Char = Cast<ACharacter>(PC->GetPawn());
			const bool bStill = Char && Char->GetCharacterMovement()->MovementMode == MOVE_None;
			const bool bRefused = !NetWorld->CastSpell(Meditate ? Meditate->Object.Id : 0);
			NetWorld->SetResting(false);
			const bool bWalks = Char && Char->GetCharacterMovement()->MovementMode != MOVE_None;
			if (bStill && bRefused && bWalks)
			{
				Pass(TEXT("rested (UC_REST): no walking or casting meanwhile; stood up (UC_STAND) and walks again"));
			}
			else
			{
				Fail(FString::Printf(TEXT("spells: resting: still %d, cast refused %d, walks after %d"), bStill, bRefused, bWalks));
			}
			Advance(EStep::Trade);
		}
		break;
	}

	case EStep::Trade:
	{
		// Raza's blacksmith (303), vaults (332) and bank (333): their keepers by name (data/net/npcs.json)
		const FMRNetWorld& W = Net->GetNetWorld();
		UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
		TSharedPtr<SMRTradeDialog> Dialog = UI ? UI->GetTradeDialog() : nullptr;
		const int32 Here = NetWorld->GetRid();
		if (Now - StepStart > 90.0 || !UI)
		{
			Fail(FString::Printf(TEXT("trade: stuck at stage %d in zone %d (%s)"), TradeStage, Here, *TradeItem));
			if (UI)
			{
				UI->CloseTrade();
			}
			Advance(EStep::Combat);
			break;
		}
		if (HopFrom && Here == HopFrom && Now - HopAt > 5.0)
		{
			++HopTry;  // that door didn't take us: the next square of it
			HopFrom = 0;
		}
		if (HopFrom && (Here == HopFrom || Here == 0))
		{
			break;
		}
		if (HopFrom)
		{
			HopFrom = 0;
			StageTime = Now;
		}
		const auto Coins = [&W]()
		{
			const FMRNetObject* C = W.Inventory.FindByPredicate([](const FMRNetObject& O) { return O.bNumber && O.Icon.Equals(TEXT("coin.bgf"), ESearchCase::IgnoreCase); });
			return C ? C->Amount : 0u;
		};
		const auto Carried = [&W](const FString& Name) { return W.Inventory.FindByPredicate([&Name](const FMRNetObject& O) { return O.Name == Name; }); };
		// in the keeper's room, next to them (the original offers trade within 5 squares)
		const auto Reach = [&](int32 Rid, const TCHAR* Name) -> uint32
		{
			if (Here != Rid)
			{
				if (Here && Now - StageTime > 1.0)
				{
					HopFrom = Here;
					StageTime = Now;
					if (!HopToward(Rid))
					{
						Fail(FString::Printf(TEXT("trade: no way from zone %d to %d"), Here, Rid));
						Advance(EStep::Combat);
					}
				}
				return 0;
			}
			for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : NetWorld->GetActors())
			{
				const AMRNetObject* A = Pair.Value.Get();
				if (A && A->GetObjectName() == Name && Now - StageTime > 1.0)
				{
					APawn* Pawn = PC->GetPawn();
					const FVector Dir = (A->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
					FVector Near = A->GetActorLocation() - Dir * 150.0;
					if (UMRZoneSubsystem* Zones = PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>(); Zones && Zones->TraceFloor(Near))
					{
						Pawn->TeleportTo(Near + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0), Dir.Rotation(), false, true);
					}
					return Pair.Key;
				}
			}
			return 0;
		};
		const auto LastChat = [&]() { const TArray<FMRChatLine>& C = Net->GetChat(); return TradeChat < C.Num() ? C.Last().Text : FString(); };
		// through the trade dialog when there is one (a window); -nullrhi has no HUD: the same requests straight
		const auto PickAndSend = [&](EMRObjectAction Action, uint32 ToId, uint32 ItemId)
		{
			if (Dialog.IsValid())
			{
				UI->DoObjectAction(Action, ToId);
				Dialog->Select(ItemId);
				Dialog->Confirm();
			}
			else if (Action == EMRObjectAction::Deposit)
			{
				Net->Deposit(ToId, {{ItemId, 0}});
			}
			else
			{
				Net->Offer(ToId, {{ItemId, 0}});
			}
		};
		const auto BuyOne = [&](uint32 ItemId)
		{
			if (Dialog.IsValid())
			{
				Dialog->Select(ItemId);
				Dialog->Confirm();
			}
			else
			{
				Net->BuyItems({{ItemId, 1}});
			}
		};
		const bool bShopShown = !Dialog.IsValid() || Dialog->GetMode() == EMRTradeMode::Shop;
		// -Render: a picture of the dialog first, then a moment for the capture
		const auto Pictured = [&](const TCHAR* Name)
		{
			if (!FApp::CanEverRender() || !Dialog.IsValid())
			{
				return true;
			}
			if (TradeShotAt == 0.0)
			{
				TradeShotAt = Now;
				const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), FString::Printf(TEXT("%s.png"), Name));
				FScreenshotRequest::RequestScreenshot(File, true, false);
				UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
				return false;
			}
			if (Now - TradeShotAt < 0.4)
			{
				return false;
			}
			TradeShotAt = 0.0;
			return true;
		};
		switch (TradeStage)
		{
		case 0:
			if (const uint32 Tomas = Reach(303, TEXT("Tomas")))
			{
				UI->DoObjectAction(EMRObjectAction::Buy, Tomas);
				TradeStage = 1;
			}
			break;
		case 1:
			if (bShopShown && W.Shop.Items.Num() > 0 && !W.Shop.bWithdrawal && Pictured(TEXT("shop")))
			{
				// the cheapest thing that isn't a number item
				const FMRNetForSale* Cheapest = nullptr;
				for (const FMRNetForSale& E : W.Shop.Items)
				{
					if (!E.Object.bNumber && (!Cheapest || E.Price < Cheapest->Price))
					{
						Cheapest = &E;
					}
				}
				if (!Cheapest)
				{
					Fail(TEXT("trade: Tomas sells nothing to buy one of"));
					Advance(EStep::Combat);
					break;
				}
				TradeItem = Cheapest->Object.Name;
				TradePrice = Cheapest->Price;
				TradeCoins = Coins();
				BuyOne(Cheapest->Object.Id);
				TradeStage = 2;
			}
			break;
		case 2:
			if (Carried(TradeItem) && Coins() < TradeCoins)
			{
				Pass(FString::Printf(TEXT("bought a %s from %s for %u shillings (BP_BUY_LIST of %d, BP_REQ_BUY_ITEMS): %u left"),
					*TradeItem, *W.Shop.Seller.Name, TradeCoins - Coins(), W.Shop.Items.Num(), Coins()));
				// sell it back: the Look dialog's Sell picks it, the offer goes, Tomas answers with a price
				TradeCoins = Coins();
				PickAndSend(EMRObjectAction::Sell, W.Shop.Seller.Id, Carried(TradeItem)->Id);
				TradeStage = 3;
			}
			break;
		case 3:
			if (W.Trade.bOpen && W.Trade.bAnswered && (!Dialog.IsValid() || Dialog->GetMode() == EMRTradeMode::Trade) && Pictured(TEXT("offer")))
			{
				const FMRNetObject* Price = W.Trade.Received.Num() ? &W.Trade.Received[0] : nullptr;
				Pass(FString::Printf(TEXT("offered the %s to %s, who offers %s (BP_OFFERED, BP_COUNTEROFFER)"), *TradeItem, *W.Trade.WithName,
					Price ? *FString::Printf(TEXT("%u %s"), Price->Amount, *Price->Name) : TEXT("nothing")));
				if (Dialog.IsValid())
				{
					Dialog->Confirm();  // Accept
				}
				else
				{
					Net->AcceptOffer();
				}
				TradeStage = 4;
			}
			break;
		case 4:
			if (!Carried(TradeItem) && Coins() > TradeCoins)
			{
				Pass(FString::Printf(TEXT("sold the %s back (BP_ACCEPT_OFFER) for %u shillings"), *TradeItem, Coins() - TradeCoins));
				TradeStage = 5;
				StageTime = Now;
			}
			break;
		case 5:
		{
			// the mace into the vault: not while wielded
			const FMRNetObject* Mace = W.Inventory.FindByPredicate([](const FMRNetObject& O) { return O.Icon.Equals(TEXT("mace.bgf"), ESearchCase::IgnoreCase); });
			if (!Mace)
			{
				Fail(TEXT("trade: no mace to put in the vault"));
				Advance(EStep::Combat);
				break;
			}
			if (Net->IsUsing(Mace->Id))
			{
				if (!bAsked)
				{
					Net->UnuseItem(Mace->Id);
					bAsked = true;
				}
				break;
			}
			if (const uint32 Bentu = Reach(332, TEXT("Bentu")))
			{
				PickAndSend(EMRObjectAction::Deposit, Bentu, Mace->Id);
				TradeChat = Net->GetChat().Num();
				TradeStage = 6;
			}
			break;
		}
		case 6:
			if (!W.Inventory.ContainsByPredicate([](const FMRNetObject& O) { return O.Icon.Equals(TEXT("mace.bgf"), ESearchCase::IgnoreCase); }))
			{
				Pass(FString::Printf(TEXT("put the mace in Bentu's vault (BP_REQ_DEPOSIT): \"%s\""), *LastChat()));
				TradeCoins = Coins();
				if (const uint32 Bentu = Reach(332, TEXT("Bentu")))
				{
					UI->DoObjectAction(EMRObjectAction::Withdraw, Bentu);  // (BP_REQ_WITHDRAWAL: no dialog needed to ask)
				}
				TradeStage = 7;
			}
			break;
		case 7:
			if (bShopShown && W.Shop.bWithdrawal)
			{
				const FMRNetForSale* Mace = W.Shop.Items.FindByPredicate([](const FMRNetForSale& E) { return E.Object.Icon.Equals(TEXT("mace.bgf"), ESearchCase::IgnoreCase); });
				if (!Mace)
				{
					Fail(FString::Printf(TEXT("trade: the vault lists %d things, not the mace"), W.Shop.Items.Num()));
					Advance(EStep::Combat);
					break;
				}
				TradePrice = Mace->Price;
				BuyOne(Mace->Object.Id);
				TradeStage = 8;
			}
			break;
		case 8:
			if (W.Inventory.ContainsByPredicate([](const FMRNetObject& O) { return O.Icon.Equals(TEXT("mace.bgf"), ESearchCase::IgnoreCase); }))
			{
				Pass(FString::Printf(TEXT("took the mace out of the vault (BP_WITHDRAWAL_LIST of %d, BP_REQ_WITHDRAWAL_ITEMS) for %u shillings"),
					W.Shop.Items.Num(), TradeCoins - Coins()));
				TradeStage = 9;
				StageTime = Now;
			}
			break;
		case 9:
			if (const uint32 Gamos = Reach(333, TEXT("Gamos")))
			{
				UI->DoObjectAction(EMRObjectAction::Bank, Gamos);
				TradeCoins = Coins();
				Net->BankDeposit(10);
				TradeStage = 10;
			}
			break;
		case 10:
			if (Coins() == TradeCoins - 10)
			{
				TradeChat = Net->GetChat().Num();
				Net->BankBalance();
				TradeStage = 11;
				StageTime = Now;
			}
			break;
		case 11:
			if (!LastChat().IsEmpty() && Now - StageTime > 0.5)
			{
				TradeItem = LastChat();
				Net->BankWithdraw(10);
				TradeStage = 12;
			}
			break;
		case 12:
			if (Coins() == TradeCoins)
			{
				Pass(FString::Printf(TEXT("10 shillings into Gamos's bank and out again (UC_DEPOSIT, UC_WITHDRAW); the balance (UC_BALANCE): \"%s\""), *TradeItem));
				UI->CloseTrade();
				Advance(EStep::Combat);
			}
			break;
		default:
			break;
		}
		break;
	}

	case EStep::Combat:
	{
		// the Outskirts of Raza: bunnies and baby spiders (data/zones.json 330 "spawning")
		constexpr int32 Outskirts = 330;
		const int32 Here = NetWorld->GetRid();
		UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
		// back where we came in (a normal move), and on
		const auto Leave = [&]()
		{
			if (APawn* Pawn = PC->GetPawn(); Pawn && !CombatHome.IsZero() && Here == Outskirts)
			{
				Pawn->TeleportTo(CombatHome, Pawn->GetActorRotation(), false, true);
			}
			Advance(EStep::Travel);
		};
		if (Now - StepStart > 90.0)  // (a fight takes longer than the other steps)
		{
			Fail(FString::Printf(TEXT("combat: stuck at stage %d in zone %d (%s, %d attacks)"), CombatStage, Here, *FoeName, Attacks));
			Leave();
			break;
		}
		if (HopFrom && (Here == HopFrom || Here == 0))
		{
			break;  // a hop under way
		}
		if (HopFrom)
		{
			HopFrom = 0;
			StepStart = Now;
			StageTime = Now;
		}
		if (CombatStage == 0)
		{
			if (Here != Outskirts)
			{
				if (Here && Now - StepStart > 1.0)
				{
					HopFrom = Here;
					if (!HopToward(Outskirts))
					{
						Fail(FString::Printf(TEXT("combat: no way from zone %d to the Outskirts"), Here));
						Advance(EStep::Travel);
					}
				}
				break;
			}
			if (Now - StageTime < 1.5)
			{
				break;  // (its creatures arrive with the room)
			}
			// the mace in hand (the items step may have put it away)
			if (const FMRNetObject* Mace = Net->GetInventory().FindByPredicate([](const FMRNetObject& O) { return O.Icon.Equals(TEXT("mace.bgf"), ESearchCase::IgnoreCase); });
				Mace && !Net->IsUsing(Mace->Id))
			{
				if (!bAsked)
				{
					Net->UseItem(Mace->Id);
					bAsked = true;
				}
				break;
			}
			// the nearest creature we can fight
			const APawn* Me = PC->GetPawn();
			const AMRNetObject* Foe = nullptr;
			for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : NetWorld->GetActors())
			{
				const AMRNetObject* A = Pair.Value.Get();
				if (A && Me && (A->GetFlags() & MRMsg::OF_ATTACKABLE) && !(A->GetFlags() & MRMsg::OF_PLAYER)
					&& (!Foe || FVector::Dist(A->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Foe->GetActorLocation(), Me->GetActorLocation())))
				{
					Foe = A;
				}
			}
			if (!Foe)
			{
				break;  // none yet: they spawn every 20 s
			}
			FoeId = Foe->GetServerId();
			FoeName = Foe->GetObjectName();
			CombatHome = Me->GetActorLocation();
			ChatBefore = Net->GetChat().Num();
			ChatSeen = ChatBefore;
			OverlaySeqBefore = Net->GetNetWorld().PlayerOverlaySeq;
			UE_LOG(LogMeridian, Display, TEXT("MRNetTest: fighting the %s (%u), %.0f m away"), *FoeName, FoeId,
				FVector::Dist(Foe->GetActorLocation(), Me->GetActorLocation()) / 100.0);
			CombatStage = 1;
		}
		else if (CombatStage == 1)
		{
			const FMRNetObject* Foe = Net->FindObject(FoeId);
			// the server's answers
			const TArray<FMRChatLine>& Chat = Net->GetChat();
			for (int32 i = FMath::Max(0, FMath::Min(ChatBefore, Chat.Num())); i < Chat.Num() && !bCombatAnswered; ++i)
			{
				if (Chat[i].Text.StartsWith(TEXT("Your ")))
				{
					Pass(FString::Printf(TEXT("the server answered our attack (BP_REQ_ATTACK): \"%s\""), *Chat[i].Text));
					bCombatAnswered = true;
				}
			}
			const FMRNetPlayerOverlay* Hand = Net->GetNetWorld().PlayerOverlays.Find(MRMsg::PWO_RIGHT_HAND);
			if (!bSwingSeen && Hand && Hand->Seq > OverlaySeqBefore && Hand->Object.Animation.Type == MRMsg::ANIMATE_ONCE)
			{
				Pass(FString::Printf(TEXT("the server swung our %s in first person (BP_PLAYER_OVERLAY, groups %d-%d)"), *Hand->Object.Icon,
					Hand->Object.Animation.GroupLow, Hand->Object.Animation.GroupHigh));
				bSwingSeen = true;
			}
			if (!bOwnSwingSeen)
			{
				const AMRCharacter* Pawn = Cast<AMRCharacter>(PC->GetPawn());
				const FName Action = Pawn && Pawn->GetSpriteBody() ? Pawn->GetSpriteBody()->GetAction() : NAME_None;
				if (Action.ToString().Contains(TEXT("attack")))
				{
					UE_LOG(LogMeridian, Display, TEXT("MRNetTest: our sprite plays %s, as the server's BP_CHANGE says"), *Action.ToString());
					bOwnSwingSeen = true;
				}
			}
			if (!bDamageShown && UI && UI->GetFloaters().ContainsByPredicate([this](const UMRUISubsystem::FFloater& F) { return F.ObjectId == FoeId; }))
			{
				const UMRUISubsystem::FFloater* F = UI->GetFloaters().FindByPredicate([this](const UMRUISubsystem::FFloater& E) { return E.ObjectId == FoeId; });
				Pass(FString::Printf(TEXT("a damage number (%s) rose over the %s"), *F->Text, *FoeName));
				bDamageShown = true;
				DamageShotAt = FApp::CanEverRender() ? Now + 0.25 : 0.0;  // (once it has risen a little)
			}
			if (DamageShotAt > 0.0 && Now >= DamageShotAt)
			{
				const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), TEXT("combat.png"));
				FScreenshotRequest::RequestScreenshot(File, true, false);
				UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
				DamageShotAt = 0.0;
			}
			if (!Foe || !(Foe->Flags & MRMsg::OF_ATTACKABLE))
			{
				if (bDamageShown)
				{
					Pass(FString::Printf(TEXT("the %s died after %d attacks (%d chosen by the attack key)"), *FoeName, Attacks, AimedAttacks));
				}
				else
				{
					Fail(FString::Printf(TEXT("combat: the %s is gone after %d attacks, but no damage was shown"), *FoeName, Attacks));
				}
				CombatStage = 2;
				StageTime = Now;
				break;
			}
			// close in and strike, a little over once a second (the server's IsOkayAttackTime); from
			// another side when the server can't see or reach it from this one (a wall, a tree between)
			for (; ChatSeen < Chat.Num(); ++ChatSeen)
			{
				if (Chat[ChatSeen].Text.StartsWith(TEXT("You can't see")) || Chat[ChatSeen].Text.StartsWith(TEXT("You can't reach")))
				{
					++ApproachSide;
				}
			}
			if (Now - LastAttackAt > 1.1 && DamageShotAt == 0.0)
			{
				if (const AMRNetObject* A = NetWorld->FindActor(FoeId))
				{
					APawn* Pawn = PC->GetPawn();
					const FVector Dir = (A->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D().RotateAngleAxis(90.0 * (ApproachSide % 4), FVector::UpVector);
					FVector Near = A->GetActorLocation() - Dir * 110.0;
					if (UMRZoneSubsystem* Zones = PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>(); Zones && Zones->TraceFloor(Near)
						&& FVector::Dist2D(Near, Pawn->GetActorLocation()) > 60.0)
					{
						Pawn->TeleportTo(Near + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0), Dir.Rotation(), false, true);
					}
					// look at it (it stands low: a bunny is knee high), so its damage number is in the picture
					const FVector Eye = Pawn->GetPawnViewLocation();
					PC->SetControlRotation(FRotator(FMath::Clamp((A->GetActorLocation() - Eye).Rotation().Pitch, -45.0, 0.0) + 8.0, Dir.Rotation().Yaw, 0.0));
				}
				NetWorld->SetTarget(FoeId);
				// the attack key's choice (the target, in view); without a view (-nullrhi) straight to the
				// server: the key's sight check would fail and say "You can't see your selected target.", the
				// server's own words, which turn the approach to another side
				if (FApp::CanEverRender() && NetWorld->Attack() == FoeId)
				{
					++AimedAttacks;
				}
				else
				{
					Net->Attack(FoeId);
				}
				++Attacks;
				LastAttackAt = Now;
			}
		}
		else if (CombatStage == 2)
		{
			// -Render: the screen effects, as the server would send them (BP_EFFECT), one picture each
			// (each lasts 600 ms; the picture 300 ms in, the next a second later)
			static const struct { uint16 Effect; int32 Ms; int32 Xlat; const TCHAR* Name; } EffectShots[] = {
				{0, 0, 0, TEXT("corpse")},  // first: the fight's end in third person
				{MRMsg::EFFECT_PAIN, 600, 0, TEXT("pain")},
				{MRMsg::EFFECT_FLASHXLAT, 600, 0x56, TEXT("flash")},
				{MRMsg::EFFECT_WHITEOUT, 600, 0, TEXT("whiteout")},
				{MRMsg::EFFECT_INVERT, 600, 0, TEXT("invert")},
				{MRMsg::EFFECT_BLUR, 600, 0, TEXT("blur")},
			};
			AMRCharacter* Pawn = Cast<AMRCharacter>(PC->GetPawn());
			if (!FApp::CanEverRender() || !Pawn || EffectShot >= 2 * UE_ARRAY_COUNT(EffectShots))
			{
				if (Now - StageTime > 1.0)
				{
					if (Pawn)
					{
						Pawn->SetViewMode(EMRViewMode::FirstPerson);
					}
					Leave();
				}
				break;
			}
			if (Now - StageTime < (EffectShot % 2 == 0 ? 1.0 : 0.3))
			{
				break;
			}
			const auto& S = EffectShots[EffectShot / 2];
			if (EffectShot % 2 == 0)
			{
				Pawn->SetViewMode(S.Effect ? EMRViewMode::FirstPerson : EMRViewMode::Chase);
				if (S.Effect)
				{
					Net->DebugEffect(S.Effect, S.Ms, S.Xlat);
				}
			}
			else
			{
				const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"),
					FString::Printf(TEXT("%s%s.png"), S.Effect ? TEXT("effect_") : TEXT("combat_"), S.Name));
				FScreenshotRequest::RequestScreenshot(File, true, false);
				UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
			}
			++EffectShot;
			StageTime = Now;
		}
		break;
	}

	case EStep::Travel:
	{
		// our zones -> Farol West (331) -> off its east edge: the Forest of Farol, built at runtime
		constexpr int32 FarolWest = 331;
		const int32 Here = NetWorld->GetRid();
		if (Net->GetPhase() == EMRNetPhase::Offline)
		{
			Fail(FString::Printf(TEXT("travel: %s"), *Net->GetLastError()));
			Advance(EStep::Logoff);
			break;
		}
		if (bTimedOut)
		{
			Fail(FString::Printf(TEXT("travel: stuck in zone %d (stage %d, loading \"%s\")"), Here, TravelStage, *NetWorld->GetLoadingRoom()));
			Advance(bDeath ? EStep::Death : EStep::Relog);
			break;
		}
		if (HopFrom && (Here == HopFrom || Here == 0))
		{
			break;  // a hop under way (Here is 0 while a runtime room builds)
		}
		if (HopFrom)
		{
			HopFrom = 0;
			StepStart = Now;  // each hop gets the full timeout
		}
		if (TravelStage == 0)
		{
			if (Here >= UMRRuntimeRooms::RuntimeRidBase)
			{
				RuntimeRid = Here;
				CheckRuntimeRoom();
				CheckRoomLight();
				SayText = FString::Printf(TEXT("Hello from the forest %d"), FMath::RandRange(100, 999));
				Net->Say(SayText);
				TravelStage = 1;
			}
			else if (Here == FarolWest && Now - StepStart > 1.0 && FApp::CanEverRender() && FlagShot == 0)
			{
				// the faction flagpole (a 3D prop of the world build, its flag the server's overlay):
				// turn to it for a picture (Saved/Screenshots/MRNet/flagpole.png)
				FlagShot = 2;
				for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : NetWorld->GetActors())
				{
					const AMRNetObject* A = Pair.Value.Get();
					if (A && A->GetObjectName().Equals(TEXT("flagpole"), ESearchCase::IgnoreCase) && PC->PlayerCameraManager)
					{
						Pass(FString::Printf(TEXT("the flagpole here is %s, %.0f m away"), A->IsShownByProp() ? TEXT("the world build's prop") : TEXT("a bitmap"),
							FVector::Dist(A->GetActorLocation(), PC->PlayerCameraManager->GetCameraLocation()) / 100.0));
						// stand 9 m off on open floor with a clear view of its top, then turn to it
						const FVector Pole = A->GetActorLocation();
						const FVector Top = Pole + FVector(0.0, 0.0, 380.0);
						UMRZoneSubsystem* ZoneSys = PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>();
						APawn* Pawn = PC->GetPawn();
						for (int32 d = 0; d < 8 && Pawn && ZoneSys; ++d)
						{
							const double Ang = FMath::DegreesToRadians(45.0 * d);
							FVector P = Pole + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * 900.0 + FVector(0.0, 0.0, 200.0);
							if (!ZoneSys->TraceFloor(P))
							{
								continue;
							}
							const FVector Eye = P + FVector(0.0, 0.0, 165.0);
							FHitResult Hit;
							FCollisionQueryParams Params(SCENE_QUERY_STAT(MRFlagView), true, Pawn);
							if (PC->GetWorld()->LineTraceSingleByChannel(Hit, Eye, Top, ECC_Visibility, Params) && FVector::Dist2D(Hit.ImpactPoint, Pole) > 100.0)
							{
								continue;
							}
							Pawn->TeleportTo(P + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0), Pawn->GetActorRotation(), false, true);
							PC->SetControlRotation((Top - Eye).Rotation());
							break;
						}
						FlagShot = 1;
						FlagShotTime = Now;
						break;
					}
				}
			}
			else if (Here == FarolWest && FlagShot == 1 && Now - FlagShotTime > 2.0)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"), TEXT("flagpole.png")), true, false);
				FlagShot = 2;
				FlagShotTime = Now;
			}
			else if (Here == FarolWest && (FlagShot == 2 || !FApp::CanEverRender()) && Now - FlagShotTime > 0.5 && Now - StepStart > 1.0)
			{
				HopFrom = Here;
				StepOffEdge(static_cast<uint8>(EMREdge::East));
			}
			else if (Here && Here != FarolWest && Now - StepStart > 1.0)
			{
				HopFrom = Here;
				if (!HopToward(FarolWest))
				{
					Fail(FString::Printf(TEXT("travel: no way from zone %d to Farol West"), Here));
					Advance(bDeath ? EStep::Death : EStep::Relog);
				}
			}
		}
		else if (TravelStage == 1 && Net->GetChat().ContainsByPredicate([this](const FMRChatLine& L) { return L.Text.Contains(SayText); }))
		{
			Pass(FString::Printf(TEXT("said \"%s\" in the runtime room and heard it back"), *SayText));
			TravelStage = 2;
			StageTime = Now;
		}
		else if (TravelStage >= 2 && TravelStage <= 5)
		{
			// rendering: a picture of the runtime room, a turn, another picture (each a moment apart: the
			// capture is at the frame's end, so a turn in the same frame comes out motion-blurred)
			const bool bRender = FApp::CanEverRender();
			if (Now - StageTime < (TravelStage % 2 == 0 ? 1.5 : 0.5))
			{
				break;
			}
			if (bRender && TravelStage % 2 == 0)
			{
				const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRNet"),
					FString::Printf(TEXT("runtime_room_%d.png"), TravelStage / 2 - 1));
				FScreenshotRequest::RequestScreenshot(File, true, false);
				UE_LOG(LogMeridian, Display, TEXT("MRNetTest: screenshot %s"), *File);
			}
			else if (bRender && TravelStage == 3)
			{
				// face the nearest creature drawn from its own bitmap, else just turn round
				const APawn* Me = PC->GetPawn();
				const AMRNetObject* Nearest = nullptr;
				for (TActorIterator<AMRNetObject> It(PC->GetWorld()); It && Me; ++It)
				{
					if (!It->GetBgfName().IsEmpty() && (!Nearest || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation())
						< FVector::Dist(Nearest->GetActorLocation(), Me->GetActorLocation())))
					{
						Nearest = *It;
					}
				}
				if (Nearest)
				{
					if (UMRNetWorldSubsystem* NW = PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>())
					{
						NW->SetTarget(Nearest->GetServerId());  // its brackets in the picture
					}
					// step to a few metres from it (a normal move: the server sees it), then look at it
					APawn* Pawn = PC->GetPawn();
					const FVector Dir = (Nearest->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
					FVector Near = Nearest->GetActorLocation() - Dir * 500.0;
					if (UMRZoneSubsystem* Zones = PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>(); Zones && Zones->TraceFloor(Near))
					{
						Pawn->TeleportTo(Near + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0), Pawn->GetActorRotation(), false, true);
					}
				}
				PC->SetControlRotation(Nearest && Me ? FRotator(-5.0, (Nearest->GetActorLocation() - Me->GetActorLocation()).Rotation().Yaw, 0.0)
					: PC->GetControlRotation() + FRotator(0.0, 150.0, 0.0));
				if (Nearest)
				{
					UE_LOG(LogMeridian, Display, TEXT("MRNetTest: looking at %s (%s), %.0f m away"), *Nearest->GetObjectName(), *Nearest->GetBgfName(),
						FVector::Dist(Nearest->GetActorLocation(), Me->GetActorLocation()) / 100.0);
				}
			}
			StageTime = Now;
			if (++TravelStage == 6)
			{
				CheckCreaturesDrawn();  // (a moment after arriving: their bitmaps download first)
				HopFrom = Here;
				StepOffEdge(static_cast<uint8>(EMREdge::West));
			}
		}
		else if (TravelStage == 6 && Here && Here != RuntimeRid)
		{
			if (Here == FarolWest)
			{
				Pass(FString::Printf(TEXT("walked off the runtime room's west edge back into Farol West (zone %d)"), Here));
			}
			else
			{
				Fail(FString::Printf(TEXT("travel: the west edge led to zone %d, not Farol West"), Here));
			}
			Advance(bDeath ? EStep::Death : EStep::Relog);
		}
		break;
	}

	case EStep::Death:
	{
		// die to the Forest of Farol's spiders (off Farol West's east edge, built at runtime), bare
		// handed so they outlast us; the Underworld; its archway back to Raza (uworld.kod: the portal
		// at row 11, col 3, to the Inn of Raza)
		constexpr int32 FarolWest = 331;
		const int32 Here = NetWorld->GetRid();
		const FString Room = Net->GetPlayer().RoomFile.ToLower();
		if (DeathStart == 0.0)
		{
			DeathStart = Now;
		}
		if (Now - DeathStart > 300.0)
		{
			Fail(FString::Printf(TEXT("death: stuck at stage %d in %s (%d attacks)"), DeathStage, *Room, Attacks));
			Advance(EStep::Relog);
			break;
		}
		if (HopFrom && Here == HopFrom && Now - HopAt > 5.0)
		{
			++HopTry;  // that door didn't take us: the next square of it
			HopFrom = 0;
		}
		if (HopFrom && (Here == HopFrom || Here == 0))
		{
			break;
		}
		HopFrom = 0;
		if (DeathStage == 0 && Room == TEXT("uworld.roo"))
		{
			DeathStage = 1;
			StageTime = Now;
		}
		if (DeathStage == 0)
		{
			if (Here < UMRRuntimeRooms::RuntimeRidBase)
			{
				if (Here && Now - StageTime > 1.0)
				{
					HopFrom = Here;
					StageTime = Now;
					if (Here == FarolWest)
					{
						StepOffEdge(static_cast<uint8>(EMREdge::East));
					}
					else if (!HopToward(FarolWest))
					{
						Fail(FString::Printf(TEXT("death: no way from zone %d to Farol West"), Here));
						Advance(EStep::Relog);
					}
				}
				break;
			}
			// bare handed
			if (const FMRNetObject* Weapon = Net->GetInventory().FindByPredicate([Net](const FMRNetObject& O) { return Net->IsUsing(O.Id) && O.Icon.Equals(TEXT("mace.bgf"), ESearchCase::IgnoreCase); }))
			{
				Net->UnuseItem(Weapon->Id);
			}
			// stir them up: strike the nearest one we haven't lately (each only every 8 s, so they gang
			// up on us rather than die), until they kill us
			if (Now - LastAttackAt > 1.1 && Now - StageTime > 1.5)
			{
				APawn* Pawn = PC->GetPawn();
				const AMRNetObject* Foe = nullptr;
				for (const TPair<uint32, TWeakObjectPtr<AMRNetObject>>& Pair : NetWorld->GetActors())
				{
					const AMRNetObject* A = Pair.Value.Get();
					const double* When = Struck.Find(Pair.Key);
					if (A && Pawn && (A->GetFlags() & MRMsg::OF_ATTACKABLE) && !(A->GetFlags() & MRMsg::OF_PLAYER) && (!When || Now - *When > 8.0)
						&& (!Foe || FVector::Dist(A->GetActorLocation(), Pawn->GetActorLocation()) < FVector::Dist(Foe->GetActorLocation(), Pawn->GetActorLocation())))
					{
						Foe = A;
					}
				}
				if (Foe)
				{
					Struck.Add(Foe->GetServerId(), Now);
				}
				if (Foe && Pawn)
				{
					const FVector Dir = (Foe->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
					FVector Near = Foe->GetActorLocation() - Dir * 110.0;
					if (UMRZoneSubsystem* Zones = PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>(); Zones && Zones->TraceFloor(Near)
						&& FVector::Dist2D(Near, Pawn->GetActorLocation()) > 60.0)
					{
						Pawn->TeleportTo(Near + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0), Dir.Rotation(), false, true);
					}
					PC->SetControlRotation(FRotator(0.0, Dir.Rotation().Yaw, 0.0));
					Net->Attack(Foe->GetServerId());
					++Attacks;
				}
				LastAttackAt = Now;
			}
		}
		else if (DeathStage == 1 && Here && PC->GetPawn() && Now - StageTime > 2.0)
		{
			Pass(FString::Printf(TEXT("died after %.0f s: the server took us to %s (uworld.roo, built at runtime as zone %d)"),
				Now - DeathStart, *Net->GetPlayer().RoomName, Here));
			// onto the archway to Raza (a normal move: the portal takes whoever steps on its square)
			if (UMRZoneSubsystem* Zones = PC->GetWorld()->GetSubsystem<UMRZoneSubsystem>())
			{
				APawn* Pawn = PC->GetPawn();
				const FVector Portal = Zones->KodToWorld(Here, 11 * MRMsg::KodFineness + 32, 3 * MRMsg::KodFineness + 32, true);
				Pawn->TeleportTo(Portal + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0), Pawn->GetActorRotation(), false, true);
			}
			DeathStage = 2;
			StageTime = Now;
		}
		else if (DeathStage == 2 && Room == TEXT("uworld.roo") && Now - StageTime > 1.0)
		{
			// a step on it: the room tells the portal of a move before the mover's own position changes
			// (room.kod SomethingMoved), so it takes us on a move that starts inside its area, as walking does
			APawn* Pawn = PC->GetPawn();
			Pawn->TeleportTo(Pawn->GetActorLocation() + FVector(Shots++ % 2 ? -40.0 : 40.0, 0.0, 0.0), Pawn->GetActorRotation(), false, true);
			StageTime = Now;
		}
		else if (DeathStage == 2 && Room != TEXT("uworld.roo") && Here)
		{
			Pass(FString::Printf(TEXT("stepped through the Underworld's archway to Raza and came back to %s (%s)"), *Net->GetPlayer().RoomName, *Room));
			Advance(EStep::Relog);
		}
		break;
	}

	case EStep::Relog:
		if (!bAsked)
		{
			CheckServerSounds();
			bAsked = true;
			bSaid = false;
			Net->ReturnToCharacters();
		}
		else if (!bSaid && Net->GetPhase() == EMRNetPhase::Characters && Net->GetStatus().IsEmpty() && !PC->GetPawn())
		{
			const FMRCharacterSlot* Named = Net->GetCharacters().FindByPredicate([](const FMRCharacterSlot& C) { return !C.bNeedsCreation; });
			if (!Named)
			{
				Fail(TEXT("log off to the characters: the list has no character"));
				Advance(EStep::Logoff);
				break;
			}
			bSaid = true;
			UE_LOG(LogMeridian, Display, TEXT("MRNetTest: back at the character list (%d slots); entering as %s again"), Net->GetCharacters().Num(), *Named->Name);
			Net->UseCharacter(Named->Id);
		}
		else if (bSaid && Net->GetPhase() == EMRNetPhase::InGame && NetWorld->GetRid() && PC->GetPawn() && Net->GetNetWorld().bHasInventory)
		{
			Pass(FString::Printf(TEXT("logged off to the character list and entered again (%s)"), *Net->GetPlayer().RoomName));
			// the hotbar kept with the character (M9): its last slot holds the item placed before
			const UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
			const FMRSlotContent Kept = UI && UI->GetSource() ? UI->GetSource()->Get(FMRSlotRef(EMRSlotArea::Hotbar, 8)) : FMRSlotContent();
			const FMRNetObject* O = Kept.ObjectId ? Net->FindInventory(Kept.ObjectId) : nullptr;
			if (!HotbarKey.IsEmpty() && O && O->Icon + TEXT("|") + O->Name == HotbarKey)
			{
				Pass(FString::Printf(TEXT("the hotbar came back with the character: %s on its last slot"), *O->Name));
			}
			else if (!HotbarKey.IsEmpty())
			{
				Fail(FString::Printf(TEXT("relog: the hotbar's last slot holds %s, not %s"), O ? *O->Name : TEXT("nothing"), *HotbarKey));
			}
			Advance(bCreated ? EStep::Delete : EStep::Logoff);
		}
		else if (bTimedOut || Net->GetPhase() == EMRNetPhase::Offline)
		{
			Fail(FString::Printf(TEXT("log off to the characters: phase %d, %s"), static_cast<int32>(Net->GetPhase()),
				Net->GetLastError().IsEmpty() ? *Net->GetStatus() : *Net->GetLastError()));
			Advance(EStep::Logoff);
		}
		break;

	case EStep::Delete:
	{
		// -Create, last: delete the new character (UC_SUICIDE, as typed "suicide" and confirmed with the
		// password): the server resets it and sends us to the character list (UC_SEND_QUIT), its slot to make anew
		const FString Name = Net->GetSelf() ? Net->GetSelf()->Name : FString();
		if (!bAsked)
		{
			bAsked = true;
			SayText = Name;
			if (!Net->IsLoginPassword(TestPassword) || Net->IsLoginPassword(TestPassword + TEXT("x")))
			{
				Fail(TEXT("delete: the password check is wrong"));
			}
			Net->DeleteCharacter();
		}
		else if (Net->GetPhase() == EMRNetPhase::Characters && Net->GetStatus().IsEmpty())
		{
			const bool bGone = !Net->GetCharacters().ContainsByPredicate([this](const FMRCharacterSlot& C) { return C.Name == SayText; });
			const bool bSlot = Net->GetCharacters().ContainsByPredicate([](const FMRCharacterSlot& C) { return C.bNeedsCreation; });
			if (bGone && bSlot)
			{
				Pass(FString::Printf(TEXT("deleted %s (UC_SUICIDE): back at the character list (UC_SEND_QUIT), the slot to create anew"), *SayText));
			}
			else
			{
				Fail(FString::Printf(TEXT("delete: back at the list, but %s is %s"), *SayText, bGone ? TEXT("gone and no slot is free") : TEXT("still there")));
			}
			Advance(EStep::Logoff);
		}
		else if (Net->GetChat().ContainsByPredicate([](const FMRChatLine& L) { return L.Text.Contains(TEXT("kill yourself")) || L.Text.Contains(TEXT("only just begun")); }))
		{
			Pass(TEXT("asked to delete the character (UC_SUICIDE): the server refused, as Kod's rules say"));
			Advance(EStep::Logoff);
		}
		else if (bTimedOut)
		{
			Fail(TEXT("delete: no answer"));
			Advance(EStep::Logoff);
		}
		break;
	}

	case EStep::Logoff:
		if (!bAsked)
		{
			Net->Logoff();
			bAsked = true;
		}
		else if (Net->GetPhase() == EMRNetPhase::Offline)
		{
			if (Net->GetLastError().IsEmpty() && !PC->GetPawn())
			{
				Pass(TEXT("logged off"));
			}
			else
			{
				Fail(FString::Printf(TEXT("log off: %s"), *Net->GetLastError()));
			}
			Finish();
		}
		else if (bTimedOut)
		{
			Fail(TEXT("log off: timed out"));
			Finish();
		}
		break;

	default:
		break;
	}
}

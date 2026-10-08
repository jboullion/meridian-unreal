#include "Tests/MRNetTest.h"

#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MeridianRemastered.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Net/MRAssetCache.h"
#include "Net/MRCharInfo.h"
#include "Net/MRNetWorld.h"
#include "Net/MRNetObject.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorldSubsystem.h"
#include "TimerManager.h"
#include "UI/MRUISubsystem.h"
#include "Engine/LocalPlayer.h"
#include "UnrealClient.h"
#include "Zones/MRZoneSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Core/MRUnits.h"
#include "GameFramework/Character.h"
#include "World/MRRuntimeRoom.h"
#include "World/MRRuntimeRooms.h"

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
			const FVector At = Zones->GridToWorld(Zone->Rid, E.Row, E.Col, true) + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0);
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
	for (const FMRZoneExit& E : Z->Exits)
	{
		if (!E.bLocked && E.DestRid == *Hop)
		{
			const FVector At = Zones->GridToWorld(From, E.Row, E.Col, true) + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + 2.0);
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
			Advance(EStep::Travel);
		}
		else if (bTimedOut || Net->GetPhase() == EMRNetPhase::Offline)
		{
			Fail(FString::Printf(TEXT("reload: %s"), Net->GetPhase() == EMRNetPhase::Offline ? *Net->GetLastError()
				: bSaid ? TEXT("our line never came back") : TEXT("the room never came back")));
			Advance(Net->GetPhase() == EMRNetPhase::Offline ? EStep::Logoff : EStep::Travel);
		}
		break;

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
			Advance(EStep::Relog);
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
				SayText = FString::Printf(TEXT("Hello from the forest %d"), FMath::RandRange(100, 999));
				Net->Say(SayText);
				TravelStage = 1;
			}
			else if (Here == FarolWest && Now - StepStart > 1.0)
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
					Advance(EStep::Relog);
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
			Advance(EStep::Relog);
		}
		break;
	}

	case EStep::Relog:
		if (!bAsked)
		{
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
		else if (bSaid && Net->GetPhase() == EMRNetPhase::InGame && NetWorld->GetRid() && PC->GetPawn())
		{
			Pass(FString::Printf(TEXT("logged off to the character list and entered again (%s)"), *Net->GetPlayer().RoomName));
			Advance(EStep::Logoff);
		}
		else if (bTimedOut || Net->GetPhase() == EMRNetPhase::Offline)
		{
			Fail(FString::Printf(TEXT("log off to the characters: phase %d, %s"), static_cast<int32>(Net->GetPhase()),
				Net->GetLastError().IsEmpty() ? *Net->GetStatus() : *Net->GetLastError()));
			Advance(EStep::Logoff);
		}
		break;

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

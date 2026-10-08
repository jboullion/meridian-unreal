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
#include "Net/MRCharInfo.h"
#include "Net/MRNetObject.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorldSubsystem.h"
#include "TimerManager.h"
#include "UI/MRUISubsystem.h"
#include "Engine/LocalPlayer.h"
#include "UnrealClient.h"
#include "Zones/MRZoneSubsystem.h"

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
	return false;
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
		if (FApp::CanEverRender() && T > 5.0 + 8.0 * Shots && Shots < 4)
		{
			// two of the world, then the dialog on the server's stats and spells (its stat groups)
			UMRUISubsystem* UI = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UMRUISubsystem>() : nullptr;
			if (UI && Shots >= 2)
			{
				UI->SetInventoryOpen(true);
				UI->SetInventoryTab(Shots == 2 ? 3 : 1);
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
				Advance(EStep::Logoff);
			}
		}
		else if (bAsked && NetWorld->GetRid() && NetWorld->GetRid() != StartRid)
		{
			Pass(FString::Printf(TEXT("took an exit from zone %d to zone %d (%s)"), StartRid, NetWorld->GetRid(), *Net->GetPlayer().RoomName));
			Advance(EStep::Logoff);
		}
		else if (bTimedOut)
		{
			Fail(FString::Printf(TEXT("exit: still in zone %d"), NetWorld->GetRid()));
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

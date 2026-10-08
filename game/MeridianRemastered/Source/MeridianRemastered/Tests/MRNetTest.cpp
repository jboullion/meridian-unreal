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
			return Pawn->TeleportTo(At, Pawn->GetActorRotation(), false, true);
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
			if (Named)
			{
				Pass(FString::Printf(TEXT("logged in: %d character slots, playing %s"), Net->GetCharacters().Num(), *Named->Name));
				Net->UseCharacter(Named->Id);
				Advance(EStep::Enter);
			}
			else if (Empty && CreateTries < 3)
			{
				const FString Name = RandomName();
				UE_LOG(LogMeridian, Display, TEXT("MRNetTest: no characters yet; creating %s"), *Name);
				++CreateTries;
				Net->CreateCharacter(Empty->Id, Name, false);
			}
			else
			{
				Fail(FString::Printf(TEXT("create a character: %s"), *Net->GetLastError()));
				Finish();
			}
		}
		else if (Net->GetPhase() == EMRNetPhase::Entering || Net->GetPhase() == EMRNetPhase::InGame)
		{
			Pass(TEXT("logged in and created a character"));
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

#include "Tests/MRSpriteNetTest.h"

#include "Character/MRCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "MeridianRemastered.h"
#include "Monsters/MRMonster.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"

bool UMRSpriteNetTest::IsRequested()
{
	FString Value;
	return FParse::Value(FCommandLine::Get(), TEXT("MRSpriteNetTest="), Value);
}

void UMRSpriteNetTest::Start(APlayerController* InController)
{
	Controller = InController;
	FParse::Value(FCommandLine::Get(), TEXT("MRSpriteNetTest="), Role);
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRSpriteNetTest::Step), 1.f, true);
}

void UMRSpriteNetTest::Step()
{
	APlayerController* PC = Controller.Get();
	AMRCharacter* Me = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	if (!Me)
	{
		return;  // not spawned yet
	}
	++Ticks;
	if (Role == TEXT("A"))
	{
		if (Ticks == 4)
		{
			FMRSpriteAppearance A;
			A.Look = TEXT("test_sword");
			A.Skin = 0;
			A.Hair = 12;
			A.Shirt = 5;
			A.Pants = 6;
			A.HeightPct = 105;
			Me->SetSpriteAppearance(A);
			UE_LOG(LogMeridian, Display, TEXT("MRSpriteNet: A set %s"), *A.ToString());
		}
		else if (Ticks == 6)
		{
			Me->PlaySpriteAction(TEXT("dance"));
			UE_LOG(LogMeridian, Display, TEXT("MRSpriteNet: A dances"));
		}
		else if (Ticks == 30)
		{
			PC->ConsoleCommand(TEXT("quit"));
		}
		return;
	}
	if (Ticks == 14)
	{
		for (TActorIterator<AMRCharacter> It(PC->GetWorld()); It; ++It)
		{
			if (*It != Me)
			{
				UE_LOG(LogMeridian, Display, TEXT("MRSpriteNet: saw %s %s action=%s"), *It->GetName(),
					*It->GetSpriteAppearance().ToString(), *It->GetReplicatedSpriteAction().ToString());
			}
		}
		// the server's monsters and NPCs reach this client with their looks
		int32 Monsters = 0, WithLook = 0;
		for (TActorIterator<AMRMonster> It(PC->GetWorld()); It; ++It)
		{
			++Monsters;
			WithLook += It->GetLook().IsNone() ? 0 : 1;
		}
		UE_LOG(LogMeridian, Display, TEXT("MRSpriteNet: monsters %d, %d with a look"), Monsters, WithLook);
		UE_LOG(LogMeridian, Display, TEXT("MRSpriteNet: DONE"));
	}
	else if (Ticks == 16)
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
}

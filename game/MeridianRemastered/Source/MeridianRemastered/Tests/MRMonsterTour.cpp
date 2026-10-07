#include "Tests/MRMonsterTour.h"

#include "Character/MRCharacter.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Character/MRSpriteData.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Monsters/MRMonster.h"
#include "Monsters/MRMonsterSubsystem.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UnrealClient.h"

bool UMRMonsterTour::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRMonsters"));
}

void UMRMonsterTour::Start(APlayerController* InController)
{
	Controller = InController;
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRMonsterTour::Next), 7.f, false);
}

void UMRMonsterTour::Spawn()
{
	APlayerController* PC = Controller.Get();
	AMRCharacter* Player = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	UMRMonsterSubsystem* Monsters = PC ? PC->GetWorld()->GetSubsystem<UMRMonsterSubsystem>() : nullptr;
	if (!Player || !Monsters)
	{
		return;
	}
	const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	TArray<FName> Classes;
	FMRSpriteLibrary::Get().Monsters.GetKeys(Classes);
	Classes.Sort(FNameLexicalLess());
	TArray<FName> Rows[2];
	for (const FName& C : Classes)
	{
		if (C != TEXT("MummyNoTreasure"))  // the same look as Mummy
		{
			Rows[FMRSpriteLibrary::Get().Monsters[C].bNpc ? 1 : 0].Add(C);
		}
	}
	for (int32 Row = 0; Row < 2; ++Row)
	{
		const float Spacing = Row == 0 ? 190.f : 150.f;
		for (int32 i = 0; i < Rows[Row].Num(); ++i)
		{
			const FVector At = Player->GetActorLocation() + Forward * (550.f + Row * 350.f)
				+ Right * ((i - (Rows[Row].Num() - 1) * 0.5f) * Spacing);
			if (AMRMonster* M = Monsters->SpawnMonster(Rows[Row][i], 0, At, (-Forward).Rotation().Yaw, false))
			{
				M->SetAIEnabled(false);
				M->SetActorRotation((-Forward).Rotation());
				Spawned.Add(M);
			}
		}
	}
	UE_LOG(LogMeridian, Display, TEXT("MRMonsters: %d spawned"), Spawned.Num());
}

void UMRMonsterTour::Shot(const FString& Name, float Delay)
{
	APlayerController* PC = Controller.Get();
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRMonsters"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = FPaths::Combine(Dir, Name + TEXT(".png"));
	PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this, File]()
	{
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogMeridian, Display, TEXT("MRMonsters: %s"), *File);
		if (APlayerController* P = Controller.Get())
		{
			P->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRMonsterTour::Next), 0.6f, false);
		}
	}), Delay, false);
}

bool UMRMonsterTour::NextZoneShot(AMRCharacter* Player)
{
	int32 StartZone = 0;
	FParse::Value(FCommandLine::Get(), TEXT("MRStartZone="), StartZone);
	TArray<AMRMonster*> InZone;
	for (TActorIterator<AMRMonster> It(Player->GetWorld()); It; ++It)
	{
		if (It->GetZone() == StartZone && !It->IsNpc() && !It->IsDead())
		{
			InZone.Add(*It);
		}
	}
	InZone.Sort([](const AMRMonster& A, const AMRMonster& B) { return A.GetName() < B.GetName(); });
	if (ZoneShots >= FMath::Min(4, InZone.Num()))
	{
		return false;
	}
	AMRMonster* M = InZone[ZoneShots * InZone.Num() / FMath::Min(4, InZone.Num())];  // spread over the zone
	// stand 4 m away where nothing is in between, looking at it
	const FVector At = M->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRMonsterTour), false, M);
	Params.AddIgnoredActor(Player);
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector Dir = FRotator(0.f, i * 45.f, 0.f).Vector();
		const FVector Stand = At + Dir * 400.f + FVector(0.f, 0.f, 20.f);
		FHitResult Hit;
		if (!Player->GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 20.f), Stand, ECC_WorldStatic, Params)
			&& Player->TeleportTo(Stand, FRotator(0.f, (-Dir).Rotation().Yaw, 0.f)))
		{
			break;
		}
	}
	// looking a little to its side, so the player doesn't hide a small monster
	Player->GetController()->SetControlRotation(FRotator(-10.f, (At - Player->GetActorLocation()).Rotation().Yaw - 12.f, 0.f));
	FString Heights;
	for (const AMRMonster* Z : InZone)
	{
		Heights += FString::Printf(TEXT(" %.0f"), Z->GetActorLocation().Z);
	}
	UE_LOG(LogMeridian, Display, TEXT("MRMonsters: zone %d has %d monsters (z:%s); shot %d: %s at %s"), StartZone, InZone.Num(),
		*Heights, ZoneShots + 1, *M->GetMonsterClass().ToString(), *At.ToCompactString());
	Shot(FString::Printf(TEXT("zone_%d"), ++ZoneShots), 1.2f);
	return true;
}

void UMRMonsterTour::Next()
{
	APlayerController* PC = Controller.Get();
	AMRCharacter* Player = PC ? Cast<AMRCharacter>(PC->GetPawn()) : nullptr;
	if (!Player)
	{
		if (PC)
		{
			PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRMonsterTour::Next), 1.f, false);
		}
		return;
	}
	auto ForEach = [this](TFunctionRef<void(AMRMonster*)> F) { for (AMRMonster* M : Spawned) { if (IsValid(M)) { F(M); } } };
	if (Step == 0)
	{
		// a zone with monsters of its own: photograph those, then quit
		Player->SetFirstPerson(false);
		if (bZoneMode && !NextZoneShot(Player))
		{
			UE_LOG(LogMeridian, Display, TEXT("MRMonsters: done"));
			PC->ConsoleCommand(TEXT("quit"));
		}
		bZoneMode = bZoneMode || NextZoneShot(Player);
		if (bZoneMode)
		{
			return;
		}
	}
	switch (Step++)
	{
	case 0:
		Player->SetFirstPerson(false);
		PC->SetControlRotation(FRotator(-10.f, Player->GetActorRotation().Yaw, 0.f));
		Spawn();
		Shot(TEXT("monsters_stand"), 2.f);
		return;
	case 1:
		ForEach([](AMRMonster* M) { if (M->GetSpriteBody()) { M->GetSpriteBody()->PlayAction(TEXT("walk")); } });
		Shot(TEXT("monsters_walk"), 0.45f);
		return;
	case 2:
		ForEach([](AMRMonster* M) { M->PlayMonsterAction(TEXT("attack")); });
		Shot(TEXT("monsters_attack"), 0.35f);
		return;
	case 3:
		// from the side: every monster turned 90 degrees
		ForEach([](AMRMonster* M) { M->SetActorRotation(M->GetActorRotation() + FRotator(0.f, 90.f, 0.f));
			if (M->GetSpriteBody()) { M->GetSpriteBody()->StopAction(); } });
		Shot(TEXT("monsters_side"), 0.6f);
		return;
	case 4:
		ForEach([](AMRMonster* M) { M->PlayMonsterAction(TEXT("attack")); });
		Shot(TEXT("monsters_side_attack"), 0.35f);
		return;
	case 5:
		ForEach([](AMRMonster* M) { if (!M->IsNpc()) { M->Die(); } });
		Shot(TEXT("monsters_dead"), 1.f);
		return;
	case 6:
	{
		// the AI: an aggressive mummy sees the player and comes; a bunny attacks only once hit
		UMRMonsterSubsystem* Monsters = PC->GetWorld()->GetSubsystem<UMRMonsterSubsystem>();
		const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Base = Player->GetActorLocation() + Forward * 200.f;
		const TPair<FName, FVector> Chase[] = {
			{TEXT("Mummy"), Base + Forward * 500.f + Right * 150.f}, {TEXT("Bunny"), Base + Forward * 250.f - Right * 150.f}};
		for (const TPair<FName, FVector>& C : Chase)
		{
			if (AMRMonster* M = Monsters ? Monsters->SpawnMonster(C.Key, 0, C.Value, (-Forward).Rotation().Yaw, false) : nullptr)
			{
				Chasers.Add(M);
				if (C.Key == TEXT("Bunny"))
				{
					M->TakePlaceholderHit(Player);
				}
			}
		}
		PC->SetControlRotation(FRotator(-10.f, Player->GetActorRotation().Yaw, 0.f));
		Shot(TEXT("monsters_chase"), 6.f);
		return;
	}
	case 7:
		for (AMRMonster* M : Chasers)
		{
			if (IsValid(M))
			{
				const float Dist = FVector::Dist2D(M->GetActorLocation(), Player->GetActorLocation());
				UE_LOG(LogMeridian, Display, TEXT("MRMonsters: chase %s dist=%.0f last=%s actions=%d"), *M->GetMonsterClass().ToString(),
					Dist, *M->GetActionState().Action.ToString(), M->GetActionState().Seq);
			}
		}
		[[fallthrough]];  // then done
	default:
		UE_LOG(LogMeridian, Display, TEXT("MRMonsters: done"));
		PC->ConsoleCommand(TEXT("quit"));
	}
}

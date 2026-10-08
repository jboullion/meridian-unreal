#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MRNetTest.generated.h"

class APlayerController;

/**
 * The online check (docs/adr/0010-meridian-servers.md), run by tools/ue/run_net_test.ps1 against
 * a Meridian server (the local Shards stack): -MRNetTest -MRServer=<name part> -MRNetUser=<u>
 * -MRNetPass=<p>, standalone and -nullrhi. It logs in (an unknown name makes the account), plays a
 * character or creates one, and checks:
 *   1. it enters a zone we have, with the pawn at the server's position;
 *   2. a line it says comes back from the server (BP_SAID);
 *   3. standing on an exit square takes it to another zone (BP_REQ_GO, a new room);
 *   4. it logs off cleanly.
 * Logs "MRNetTest: PASS/FAIL ..." per step and "MRNetTest: DONE <passed>/<steps>", then quits.
 * -MRNetHold=<seconds> stays that long after saying hello (for a look from another client): it
 * steps back and forth, logs the creatures it draws and, when rendering, takes screenshots into
 * Saved/Screenshots/MRNet/.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRNetTest : public UObject
{
	GENERATED_BODY()

public:
	static bool IsRequested();
	void Start(APlayerController* InController);

private:
	enum class EStep : uint8 { Login, Enter, Say, Hold, Exit, Logoff, Done };

	void Tick();
	void Pass(const FString& What);
	void Fail(const FString& What);
	void Finish();
	bool TakeExit();
	void LogCreatures() const;

	TWeakObjectPtr<APlayerController> Controller;
	EStep Step = EStep::Login;
	double StepStart = 0.0;
	int32 Passed = 0;
	int32 Steps = 0;
	int32 CreateTries = 0;
	int32 StartRid = 0;
	bool bAsked = false;
	float HoldSeconds = 0.f;
	int32 HoldMoves = 0;
	int32 Shots = 0;
	/** -Render -Hold: when the character page was captured (0: not yet). */
	double CharacterShotAt = 0.0;
	FString SayText;
	FTimerHandle Timer;
};

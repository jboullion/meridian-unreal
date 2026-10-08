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
 *   0. a new character (an account without one; run_net_test.ps1 -Create makes a fresh account) is
 *      made through the creator's path: the server's options (BP_CHARINFO), then a female with
 *      chosen face parts and colours, the Mage stats and a spell and a skill (BP_NEW_CHARINFO), and
 *      the server then shows that face on our object;
 *   1. it enters a zone we have, with the pawn at the server's position;
 *   2. a line it says comes back from the server (BP_SAID);
 *   3. standing on an exit square takes it to another zone (BP_REQ_GO, a new room);
 *   4. the room's .roo comes through the asset cache, matching the manifest and the server's
 *      security value, and the players list (BP_PLAYERS) has our character;
 *   5. reloading the data (what BP_INVALIDATE_DATA does after a server save) brings the room back,
 *      and chat still works after;
 *   6. travel: it walks our zones' exits to Farol West and off its east edge into the Forest of Farol,
 *      a room we haven't built: it is built from the server's files (UMRRuntimeRooms), the pawn
 *      stands on its floor, chat works there, and walking off its west edge comes back;
 *   7. Log Off returns to the character list (BP_REQ_QUIT, BP_QUIT, the server's menu) and the
 *      character enters again;
 *   8. it logs off cleanly.
 * Entering a zone also checks that ours was built from the server's room (its security value).
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
	enum class EStep : uint8 { Login, Enter, Say, Hold, Exit, Assets, Reload, Travel, Relog, Logoff, Done };

	void Tick();
	void Pass(const FString& What);
	void Fail(const FString& What);
	void Finish();
	bool TakeExit();
	/** One hop toward TargetRid through our zones' tile and edge exits (false: no way there). */
	bool HopToward(int32 TargetRid);
	/** Teleport the pawn just past an edge of its zone's grid: the server takes the edge exit. */
	bool StepOffEdge(uint8 Edge);
	/** The Travel step's checks, run in the runtime room. */
	void CheckRuntimeRoom();
	/** Every creature in the room is drawn (a sprite of ours, or the server's bitmap). */
	void CheckCreaturesDrawn();
	/** The new character the creator path sends (and what our object should then wear). */
	void SendNewCharacter(class UMRNetSubsystem* Net);
	void CheckLook(class UMRNetSubsystem* Net);
	void LogCreatures() const;

	TWeakObjectPtr<APlayerController> Controller;
	EStep Step = EStep::Login;
	double StepStart = 0.0;
	int32 Passed = 0;
	int32 Steps = 0;
	int32 CreateTries = 0;
	bool bCreated = false;
	/** hotspot -> bgf, and the skin and hair translations the new character asked for. */
	TMap<uint8, FString> ExpectedParts;
	int32 ExpectedSkinXlat = -1;
	int32 ExpectedHairXlat = -1;
	int32 StartRid = 0;
	bool bAsked = false;
	float HoldSeconds = 0.f;
	int32 HoldMoves = 0;
	int32 Shots = 0;
	/** -Render -Hold: when the character page was captured (0: not yet). */
	double CharacterShotAt = 0.0;
	FString SayText;
	FTimerHandle Timer;
	/** The asset cache's answer (Assets step). */
	bool bAssetDone = false;
	FString AssetResult;
	bool bAssetOk = false;
	/** Rooms entered before the reload. */
	int32 RoomsBefore = 0;
	bool bSaid = false;
	/** Travel: the zone a hop left from (0: none under way), the runtime room's zone, what's done there. */
	int32 HopFrom = 0;
	int32 RuntimeRid = 0;
	int32 TravelStage = 0;
	double StageTime = 0.0;
};

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
 *   6. look: every object in the room has an actor (our sprite, its bitmap, or a prop of the world
 *      build); BP_REQ_LOOK on a named object brings its description; on ourselves UC_LOOK_PLAYER,
 *      editable, and a new description (BP_CHANGE_DESCRIPTION) comes back in the next look;
 *   7. items: the inventory and what is in use come (BP_INVENTORY, BP_USE_LIST) and the inventory
 *      screen shows them; an item is used and put away, or taken off and put on (BP_REQ_USE,
 *      BP_REQ_UNUSE; in use, it shows on its equipment slot), dropped (BP_REQ_DROP: it leaves the
 *      inventory and lies in the room) and picked up again (BP_REQ_GET); 10 of a number item are
 *      dropped and picked up (the tagged id and an amount);
 *   7b. spells: the server's spells and skills (BP_SPELLS, BP_SKILLS) are on the Spells page, the
 *      room's enchantments came (the Inn is a safe room), the quests (stat group 5) are listed;
 *      appraise is cast on the mace, chosen as its target (BP_REQ_CAST with one target), then
 *      meditate (no target), each answered by the server; resting stops walking until we stand;
 *   7c. trade: Tomas the blacksmith's list (BP_REQ_BUY, BP_BUY_LIST) and buying its cheapest thing
 *      through the trade dialog (BP_REQ_BUY_ITEMS); selling it back (BP_REQ_OFFER, his price as
 *      BP_COUNTEROFFER, BP_ACCEPT_OFFER); the mace into Bentu's vault and out again for its fee
 *      (BP_REQ_DEPOSIT, BP_REQ_WITHDRAWAL, BP_WITHDRAWAL_LIST, BP_REQ_WITHDRAWAL_ITEMS); 10 shillings
 *      into Gamos's bank and out again, and the balance (UC_DEPOSIT, UC_WITHDRAW, UC_BALANCE);
 *   8. combat: in the Outskirts of Raza it fights a bunny or a baby spider until it dies
 *      (BP_REQ_ATTACK): the server answers with hit or miss lines, swings our weapon in first person
 *      (BP_PLAYER_OVERLAY), a damage number rises over what we hit, and the creature is gone. With
 *      -Render: combat.png, and the screen effects as the server would send them (effect_*.png);
 *   9. travel: it walks our zones' exits to Farol West and off its east edge into the Forest of Farol,
 *      a room we haven't built: it is built from the server's files (UMRRuntimeRooms), the pawn
 *      stands on its floor, chat works there, and walking off its west edge comes back;
 *   (-MRNetDeath, run_net_test.ps1 -Death: a fresh character dies bare handed to the Forest of Farol's spiders,
 *      the server takes it to the Underworld (uworld.roo, built at runtime), and it walks into the
 *      archway back to the Inn of Raza; the character loses what death costs, so not by default)
 *   10. Log Off returns to the character list (BP_REQ_QUIT, BP_QUIT, the server's menu) and the
 *      character enters again;
 *   11. it logs off cleanly.
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
	enum class EStep : uint8 { Login, Enter, Say, Hold, Exit, Assets, Reload, Look, Items, Spells, Trade, Combat, Travel, Death, Relog, Logoff, Done };

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
	void CheckRoomLight();
	void CheckServerSounds();
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
	/** When the hop under way started, and how many have failed (each retry takes the next exit square). */
	double HopAt = 0.0;
	int32 HopTry = 0;
	int32 RuntimeRid = 0;
	int32 TravelStage = 0;
	double StageTime = 0.0;
	/** Look: what we asked about, how many descriptions have come, and our marker text. */
	uint32 LookId = 0;
	int32 Descriptions = 0;
	int32 DescriptionsBefore = 0;
	int32 LookStage = 0;
	FString Marker;
	/** Items: the stage, the item tried (its name, whether it was in use) and its id in the room once dropped. */
	int32 ItemStage = 0;
	uint32 ItemId = 0;
	FString ItemName;
	bool bItemWasInUse = false;
	uint32 DroppedId = 0;
	uint32 CoinsBefore = 0;
	/** Spells: the stage and the chat read so far. */
	int32 SpellStage = 0;
	int32 SpellChat = 0;
	/** -Render: the Quests page and the target hint pictures taken so far. */
	int32 SpellShot = 0;
	/** Trade: the stage, the thing bought, its price, the shillings before, and the chat read so far. */
	int32 TradeStage = 0;
	FString TradeItem;
	uint32 TradePrice = 0;
	uint32 TradeCoins = 0;
	int32 TradeChat = 0;
	/** -Render: when the shop or the offer was pictured (0: not yet). */
	double TradeShotAt = 0.0;
	/** Combat: the stage, the creature fought, attacks sent, the chat and first-person counts before, what was seen. */
	int32 CombatStage = 0;
	uint32 FoeId = 0;
	FString FoeName;
	int32 Attacks = 0;
	int32 AimedAttacks = 0;
	double LastAttackAt = 0.0;
	int32 ChatBefore = 0;
	uint32 OverlaySeqBefore = 0;
	bool bCombatAnswered = false;
	bool bSwingSeen = false;
	bool bDamageShown = false;
	double DamageShotAt = 0.0;
	bool bOwnSwingSeen = false;
	int32 EffectShot = 0;
	/** Which side we strike from (a quarter turn each time the server can't see or reach it), and the chat read so far. */
	int32 ApproachSide = 0;
	int32 ChatSeen = 0;
	/** Where we came into the Outskirts: travel goes on from there (its edge search starts where we stand). */
	FVector CombatHome = FVector::ZeroVector;
	/** -MRNetDeath: die and come back (its own time limit: dying takes a while). */
	bool bDeath = false;
	int32 DeathStage = 0;
	double DeathStart = 0.0;
	/** When each mummy was last struck: each only now and then, so they gang up rather than die. */
	TMap<uint32, double> Struck;
};

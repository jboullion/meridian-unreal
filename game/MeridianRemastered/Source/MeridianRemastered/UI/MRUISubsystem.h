#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/MRUITypes.h"
#include "MRUISubsystem.generated.h"

class AMRAvatarPreview;
class APlayerController;
class IToolTip;
class SMRHUDRoot;
class SWidget;
class UMRAttributeSet;
class UMRGameDataSubsystem;
class UMRInventorySource;
class UMRUIStyle;
struct FSlateBrush;

/**
 * The in-game UI for one local player (docs/adr/0009-user-interface.md): the HUD (vitals, item
 * hotbar, spell bar, minimap) and the inventory dialog (Inventory, Spells, Skills, Stats, Quests),
 * drawn with Slate in C++ over the original client's art.
 *
 * AMRHUD shows it for a local player that can draw (never on a dedicated server or -nullrhi);
 * AMRPlayerController feeds it the UI keys (1-9 and the wheel select the hotbar, numpad 1-9 cast,
 * E / I open the dialog, - / = zoom the map).
 */
enum class EMRPickAction : uint8;

/** The windows for the others (UI/SMRSocial.h). */
enum class EMRWindow : uint8
{
	Who,
	Mail,
	News,
	Guild,
	Count,
};
enum class EMRObjectAction : uint8;

UCLASS()
class MERIDIANREMASTERED_API UMRUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Create the HUD in the viewport for this controller (once). */
	void ShowHUD(APlayerController* PC);
	void RemoveHUD();
	bool HasHUD() const { return HUD.IsValid(); }

	/** The login screen (playing on a Meridian server; UMRNetWorldSubsystem decides when). */
	void ShowLogin(APlayerController* PC);
	void HideLogin();
	bool IsLoginShown() const { return Login.IsValid(); }
	/** The character creator on the login screen (tests), or null. */
	TSharedPtr<class SMRCharCreator> GetCreator() const;

	/** Start typing a chat line (Enter, online). */
	void OpenChat();
	/** The chat line was said or cancelled. */
	void OnChatClosed();
	bool IsChatOpen() const { return bChatOpen; }
	/** Open the chat line with this already typed (a tell from the who list). */
	void OpenChatWith(const FString& Text);
	/**
	 * Run a typed line (MRChat::Interpret): speech, or one of the original's commands (say, tell,
	 * yell, broadcast, emote, who, mail, guild, wave, rest, cast, deposit, safety on...). UI/MRUIChat.cpp.
	 */
	void RunChatLine(const FString& Line);

	// --- the windows for the others (who, mail, news, guild)
	void SetWindowOpen(EMRWindow Window, bool bOpen);
	void ToggleWindow(EMRWindow Window) { SetWindowOpen(Window, !IsWindowOpen(Window)); }
	bool IsWindowOpen(EMRWindow Window) const { return (OpenWindows & (1u << static_cast<uint32>(Window))) != 0; }
	bool IsAnyWindowOpen() const { return OpenWindows != 0; }
	TSharedPtr<class SMRSocialWindow> GetWindow(EMRWindow Window) const;

	// --- keys (from AMRPlayerController)
	void OnHotbarKey(int32 Index);
	void OnHotbarScroll(float Delta);
	void OnSpellKey(int32 Index);
	void OnMapZoom(float Steps);
	void ToggleInventory();
	void SetInventoryOpen(bool bOpen);
	/** Show a page of the dialog (EMRInventoryTab order: inventory, spells, skills, stats, quests). */
	void SetInventoryTab(int32 Tab);
	/** Tests (-MRUIShots): the dialog's search text on a page (1 spells, 2 skills), fold a spell school. */
	void DebugSearch(int32 Tab, const FString& Text);
	void DebugFoldSpellSchool(int32 School);
	bool IsInventoryOpen() const { return bInventoryOpen; }

	// --- the Escape menu (SMRGameMenu)
	void ToggleGameMenu();
	void SetGameMenuOpen(bool bOpen);
	bool IsGameMenuOpen() const { return bGameMenuOpen; }
	/** Playing on a server: Log Off goes back to the character list. */
	bool CanLogOff() const;
	void LogOffToCharacters();
	/** Leave the server (if on one) and close the game. */
	void QuitGame();

	// --- Look (SMRLookDialog; the server's descriptions: UMRNetSubsystem::OnDescription)
	/** Several things under the crosshair: list them to pick one to look at. */
	void ShowLookPicker(const TArray<uint32>& Ids);
	/** Room objects to pick from, for an action (a pile to pick up from). */
	void ShowPicker(const TArray<uint32>& Ids, EMRPickAction Action);
	/** A row was picked: look at it, pick it up, or take it from the container. */
	void PickChosen(EMRPickAction Action, uint32 ObjectId);
	/** A Look dialog button: get, look inside, activate. */
	void DoObjectAction(EMRObjectAction Action, uint32 ObjectId);
	/** Ask the server for an object's description (it opens the dialog when it arrives). */
	void LookAt(uint32 ObjectId);
	/** Save one's own description and show it again. */
	void SaveDescription(uint32 ObjectId, const FString& Text);
	void CloseLook();
	bool IsLookOpen() const { return bLookOpen; }
	TSharedPtr<class SMRLookDialog> GetLookDialog() const;

	// --- slots (from the slot widgets)
	void OnSlotMouseDown(const FMRSlotRef& Slot, bool bRight, bool bShift);
	void OnSlotMouseUp(const FMRSlotRef& Slot);
	void SetHoveredSlot(const FMRSlotRef& Slot, bool bHovered);
	const FMRSlotRef& GetHoveredSlot() const { return HoveredSlot; }
	/** The mouse's last position as the UI's events saw it (absolute): the carried stack is drawn there. */
	void NoteMouse(const FVector2D& ScreenSpace) { MouseScreen = ScreenSpace; bHasMouse = true; }
	bool GetMouse(FVector2D& OutScreenSpace) const { OutScreenSpace = MouseScreen; return bHasMouse; }
	/** Clicked outside the dialog window. */
	void OnClickOutside(bool bRight);

	// --- what the widgets draw
	UMRInventorySource* GetSource() const { return Source; }
	UMRUIStyle* GetStyle() const;
	UMRGameDataSubsystem* GetData() const;
	UMRAttributeSet* GetAttributes() const;
	APlayerController* GetPlayerController() const;
	const FSlateBrush* IconFor(const FMRSlotContent& Content) const;
	/**
	 * A server object's picture from its own bitmap (its group, seen from the front), made when its
	 * file arrives through the asset cache: items we have no prebuilt icon for. Null until then.
	 */
	const FSlateBrush* BitmapIcon(const FString& Bgf, int32 Group) const;
	FText NameFor(const FMRSlotContent& Content) const;
	TSharedPtr<IToolTip> MakeToolTip(const FMRSlotContent& Content);
	/** A skill's tooltip: its name, school, percentage and description. */
	TSharedPtr<IToolTip> MakeSkillToolTip(FName Skill, int32 Percent);
	/** Spell bar cooldown left (0..1) after a cast. */
	float SpellCooldown(int32 Index) const;
	/** Seconds since the spell bar was last used (it shows fully for a moment). */
	double SpellBarLastUsed() const { return LastSpellKeyTime; }
	/** Time of the last hotbar selection change (the item name fades out after it). */
	double LastSelectionTime() const { return SelectionTime; }
	float GetMapZoom() const { return MapZoom; }
	/** The avatar render target (null until the dialog has been opened). */
	UObject* GetAvatarTarget() const;
	/** Turn the avatar by a number of the original's eight angles. */
	void TurnAvatar(int32 Steps);

	// --- the character creator's previews (SMRCharCreator): the whole body and the face
	void SetCreatorAppearance(const struct FMRSpriteAppearance& Appearance);
	void SetCreatorCapturing(bool bCapture);
	UObject* GetCreatorTarget(bool bPortrait) const;
	void TurnCreatorBody(int32 Steps);
	/** Turn the creator's face preview (the head from every side). */
	void TurnCreatorPortrait(int32 Steps);
	double Now() const;
	/** Where each slot was last drawn (absolute / desktop pixels): -MRUIShots points the mouse at one. */
	void NoteSlotDrawn(const FMRSlotRef& Slot, FVector2f AbsoluteCentre) { SlotCentres.Add(SlotKey(Slot), AbsoluteCentre); }
	bool GetSlotCentre(const FMRSlotRef& Slot, FVector2f& OutAbsolute) const;
	/**
	 * The Stats page: the server's stat list when playing online (its stat group 2: the server
	 * decides which stats exist), else the mock list, sorted into sections by
	 * data/ui/stat_layout.json for the server's ruleset.
	 */
	void GetStatSections(TArray<FMRStatSection>& Out) const;
	/** Bumped whenever the server's stats change (the Stats page rebuilds). */
	int32 GetStatsVersion() const { return StatsVersion; }
	/** Health (0), mana (1) or vigor (2): the server's (its stat group 1) online, else the local attributes. */
	bool GetVital(int32 Index, float& OutValue, float& OutMax) const;
	/** A text box in the dialog has the keyboard (the spell search): every key goes to it. */
	void SetTextInput(bool bTyping);
	// --- trade (SMRTradeDialog; docs/adr/0012 M6)
	void CloseTrade();
	bool IsTradeOpen() const { return bTradeOpen; }
	TSharedPtr<class SMRTradeDialog> GetTradeDialog() const;
	/** An item's icon by its bitmap: our prebuilt one for its class, else the bitmap itself (null until it arrives). */
	const FSlateBrush* ItemIcon(const FString& Icon) const;

	// --- retraining (SMRStatChange; the server's BP_STAT_CHANGE)
	/** Send the six stats (BP_CHANGED_STATS); the dialog closes on the server's answer. */
	void SubmitStatChange(const int32 (&Values)[6]);
	void CloseStatChange();
	bool IsStatChangeOpen() const { return bStatChangeOpen; }
	/** UI shots: the dialog with this offer, without a server. */
	void DebugShowStatChange(const struct FMRNetStatChange& Offer);

	/** The Quests page: the server's quest group (5), headings and quests in its order. */
	void GetQuests(TArray<FMRQuestView>& Out) const;
	/** Look at a quest (BP_REQ_LOOK on its object: its description in the Look dialog). */
	void LookAtQuest(uint32 ObjectId);
	/** An enchantment's icon: its spell's, else its own bitmap (null until that arrives). */
	const FSlateBrush* EnchantmentIcon(const struct FMRNetObject& O) const;
	/** What U uses on something: the item under the mouse in the dialog, else the selected hotbar item (0: none). */
	uint32 ItemToApply() const;
	/** The minimap picture of a geometry zone (kept loaded), or null if it hasn't been captured. */
	class UTexture2D* GetMapTexture(int32 GeometryRid);

	/**
	 * A damage number rising over what was hit and fading (ours: the original only printed the line;
	 * docs/adr/0012 M4): the damage we deal over our target, what we take over ourselves. mr.UI.DamageNumbers 0 hides them.
	 */
	struct FFloater
	{
		uint32 ObjectId = 0;
		FString Text;
		FLinearColor Color = FLinearColor::White;
		double Start = 0.0;
	};
	const TArray<FFloater>& GetFloaters() const { return Floaters; }
	/** How long a damage number shows, in seconds. */
	static constexpr double FloaterSeconds = 1.4;

private:
	/** What the widgets show: Mock offline, an UMRNetInventory while in a server's game. */
	UPROPERTY(Transient)
	TObjectPtr<UMRInventorySource> Source;

	UPROPERTY(Transient)
	TObjectPtr<UMRInventorySource> Mock;

	UPROPERTY(Transient)
	TObjectPtr<AMRAvatarPreview> Avatar;

	UPROPERTY(Transient)
	TObjectPtr<AMRAvatarPreview> CreatorBody;

	UPROPERTY(Transient)
	TObjectPtr<AMRAvatarPreview> CreatorPortrait;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<class UTexture2D>> MapTextures;

	TWeakObjectPtr<APlayerController> OwnerPC;
	TSharedPtr<SMRHUDRoot> HUD;
	TSharedPtr<class SMRLoginScreen> Login;
	bool bInventoryOpen = false;
	bool bChatOpen = false;
	bool bGameMenuOpen = false;
	bool bLookOpen = false;
	/** BitmapIcon's pictures, by "bgf:group" (a null brush: asked for, not here yet). */
	mutable TMap<FString, TSharedPtr<FSlateBrush>> BitmapIcons;
	UPROPERTY(Transient)
	mutable TMap<FString, TObjectPtr<class UTexture2D>> BitmapIconTextures;
	/** The Look dialog's picture: an object's own bitmap, or a player's face portrait. */
	FSlateBrush LookBrush;
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> LookPicture;
	uint32 LookPictureFor = 0;
	FDelegateHandle NetDescriptionHandle;
	FDelegateHandle NetContentsHandle;
	FDelegateHandle NetHitHandle;
	FDelegateHandle NetAbilitiesHandle;
	FDelegateHandle NetStatChangeHandle;
	FDelegateHandle NetStatChangeResultHandle;
	bool bStatChangeOpen = false;
	bool bTradeOpen = false;
	uint32 OpenWindows = 0;
	FDelegateHandle NetNewsHandle;
	FDelegateHandle NetGuildHandle;
	void OnNetNews();
	void OnNetGuild();
	FDelegateHandle NetShopHandle;
	FDelegateHandle NetTradeHandle;
	void SetTradeOpen(bool bOpen);
	void OnNetShop();
	void OnNetTrade();
	void SetStatChangeOpen(bool bOpen);
	void OnNetStatChange();
	void OnNetStatChangeResult(bool bOk);
	/** Our spells (by class) to the server's spell objects: what BP_REQ_CAST names. */
	TMap<FName, uint32> SpellIds;
	/** The spells and skills pages from the server's lists (BP_SPELLS, BP_SKILLS) and percentages (stat groups 3, 4). */
	void RebuildAbilities();
	TArray<FFloater> Floaters;
	void OnNetHit(const struct FMRNetHit& Hit);
	void OnNetDescription();
	/** A container's contents arrived (BP_OBJECT_CONTENTS): list them to take from. */
	void OnNetContents();
	void SetLookOpen(bool bOpen);
	bool bTextInput = false;
	int32 StatsVersion = 0;
	FDelegateHandle NetStatsHandle;
	FDelegateHandle NetPhaseHandle;
	void OnNetStats(uint32 Group);
	/** In a server's game: a fresh UMRNetInventory for this character; out of it: the mock. */
	void OnNetPhase();
	void UseSource(UMRInventorySource* InSource);
	class UMRNetSubsystem* GetNet() const;
	FMRSlotRef HoveredSlot;
	/** Where the mouse went down with an empty cursor: releasing over another slot places (drag and drop). */
	FMRSlotRef PressSlot;
	bool bPickedOnPress = false;
	double SpellCastTime[9] = {};
	double LastSpellKeyTime = -100.0;
	double SelectionTime = -100.0;
	float MapZoom = 1.f;

	TMap<uint32, FVector2f> SlotCentres;
	FVector2D MouseScreen = FVector2D::ZeroVector;
	bool bHasMouse = false;
	static uint32 SlotKey(const FMRSlotRef& Slot) { return (static_cast<uint32>(Slot.Area) << 16) | static_cast<uint32>(Slot.Index & 0xFFFF); }

	void ApplyInputMode();
	TSharedPtr<IToolTip> FramedToolTip(const FText& Title, const FText& Line, const FText& Desc);
	void EnsureAvatar();
	void EnsureCreatorPreviews();
	AMRAvatarPreview* SpawnPreview(const FVector& Offset);
	void OnStyleReloaded();
	FDelegateHandle StyleHandle;
};

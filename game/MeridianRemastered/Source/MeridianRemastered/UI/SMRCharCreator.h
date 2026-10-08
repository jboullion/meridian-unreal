#pragma once

#include "CoreMinimal.h"
#include "Character/MRSpriteAppearance.h"
#include "Net/MRCharInfo.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;
class SMRSelectList;
class SMRTextBox;
class SMRTextField;
class STextBlock;
class UMRNetSubsystem;
class UMRUISubsystem;

/**
 * The character creator (the original's "Customize your character", module/char): five pages,
 * Name, Appearance, Statistics, Spells and Skills, over what the server offers (BP_CHARINFO,
 * UMRNetSubsystem::GetCharInfo). OK checks the choices as the server will (MRCharInfo::Validate)
 * and sends them (BP_NEW_CHARINFO); the server answers with the game or a refusal. Cancel goes back
 * to the character list. The preview shows the face as the original's did and the whole sprite
 * body, turnable, both drawn by the game's own sprite body (UMRUISubsystem's creator previews).
 * Words and stat presets: data/ui/char_create.json. Shown by SMRLoginScreen in the Creating phase.
 */
class MERIDIANREMASTERED_API SMRCharCreator : public SCompoundWidget
{
public:
	enum class ETab : uint8 { Name, Appearance, Stats, Spells, Skills, Count };

	SLATE_BEGIN_ARGS(SMRCharCreator) {}
	SLATE_END_ARGS()

	~SMRCharCreator();
	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);

	/** New options arrived (or the creator opened): start a fresh, random character. */
	void Begin(int32 Seed = -1);
	/** Focus the page's first field. */
	void OnShown();
	void SetTab(ETab InTab);
	ETab GetTab() const { return Tab; }
	/** The character being made (tests). */
	FMRNewCharacter& GetCharacter() { return State; }
	/** The sprite appearance of a new character (player_male / player_female with its face parts and colours). */
	static FMRSpriteAppearance AppearanceOf(const FMRNewCharacter& C, const FMRCharInfo& Info);
	/** Apply a stat preset (data/ui/char_create.json order). */
	void ApplyPreset(int32 Index);
	/** Add a spell or skill by its position in the server's list (tests). */
	void AddAbility(bool bSkill, int32 InfoIndex);
	/** Refresh everything shown from State (after a test changed it). */
	void Refresh();
	/** Fill the Name page (tests). */
	void SetNameAndDescription(const FString& Name, const FString& Description);
	/** Give the description box the keyboard (tests). */
	void FocusDescription();
	FString GetDescriptionText() const;
	/** Press OK (tests). */
	void PressOk() { Ok(); }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event) override;

private:
	UMRNetSubsystem* GetNet() const;
	const FMRCharInfo& Info() const;
	void LoadConfig();
	FString Prompt(const TCHAR* Key) const;

	TSharedRef<SWidget> MakeNamePage();
	TSharedRef<SWidget> MakeAppearancePage();
	TSharedRef<SWidget> MakeStatsPage();
	TSharedRef<SWidget> MakeAbilityPage(bool bSkills);
	TSharedRef<SWidget> MakePicker(const FText& Label, TFunction<void(int32)> Step, TFunction<FText()> Value);
	TSharedRef<SWidget> MakePointsBar(const FText& Label, TFunction<float()> Left, float Max);

	void PushAppearance();
	void RebuildAbilityLists();
	FText AbilityLabel(const FMRCharAbility& A, bool bSkill) const;
	/** Why an available spell or skill can't be added now (empty: it can). */
	FString CantAdd(bool bSkill, int32 InfoIndex) const;
	void AddSelected(bool bSkill);
	void RemoveSelected(bool bSkill);
	FText SelectedInfo(bool bSkill) const;
	void Ok();
	/** Points still to spend (stats; spells and skills while one still fits): shows that page, says so. */
	bool UnspentPoints(FString& OutMessage);
	void Cancel();

	TWeakObjectPtr<UMRUISubsystem> UI;
	TSharedPtr<FJsonObject> Config;
	FMRNewCharacter State;
	ETab Tab = ETab::Name;
	FString LocalError;

	TSharedPtr<SMRTextField> NameField;
	TSharedPtr<SMRTextBox> DescBox;
	// per list: the rows' indexes into the server's spells / skills
	TSharedPtr<SMRSelectList> Available[2];
	TSharedPtr<SMRSelectList> Chosen[2];
	TArray<int32> AvailableRows[2];
	TArray<int32> ChosenRows[2];
	/** Which list's selection the info text describes (true: the chosen list). */
	bool bInfoFromChosen[2] = {false, false};

	FDelegateHandle CharInfoHandle;
	FDelegateHandle CreateFailedHandle;
};

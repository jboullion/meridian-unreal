#pragma once

#include "CoreMinimal.h"
#include "UI/SMRSocial.h"

/**
 * Help (the Escape menu's Help; F1 offline), after Meridian Shards' HelpDialog.tsx
 * (docs/adr/0009-user-interface.md, "The Shards layout, second pass"):
 *   - Controls: a keyboard with each key coloured by what it does (movement, interaction, targeting,
 *     communication, the interface and quick slots, the map, the quick chat), from the bindings now
 *     (MRKeys) and the fixed keys; pointing at a group lights its keys, at a key names its uses. The
 *     mouse's buttons under it.
 *   - HUD: the screen as it is, each part of the HUD outlined and numbered, with a list of what they
 *     are (SMRHUDRoot::GetHudParts). Shards takes a picture; ours points at the live HUD.
 *   - Interface: the Escape menu, the inventory dialog's pages and the other windows.
 */
class UNREALMERIDIAN_API SMRHelpDialog : public SMRSocialWindow
{
public:
	SLATE_BEGIN_ARGS(SMRHelpDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual void Rebuild() override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

	/** "Controls", "HUD" or "Interface". */
	void SetPage(const FString& InPage);
	const FString& GetPage() const { return Page; }

	/** The group a key's use belongs to (Shards' GROUPS), or -1. */
	static int32 GroupOf(FName Binding);

private:
	TSharedRef<SWidget> MakeControls();
	TSharedRef<SWidget> MakeHud();
	TSharedRef<SWidget> MakeInterface();

	FString Page = TEXT("Controls");
	/** The group pointed at on the Controls page (its keys lit), or -1. */
	int32 FocusGroup = INDEX_NONE;
	/** The HUD part pointed at in the HUD page's list (its outline lit), or -1. */
	int32 HotPart = INDEX_NONE;

	friend class SMRHelpKey;
	friend class SMRHelpLine;
};

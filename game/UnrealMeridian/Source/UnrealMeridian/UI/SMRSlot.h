#pragma once

#include "CoreMinimal.h"
#include "UI/MRUITypes.h"
#include "Widgets/SLeafWidget.h"

class UMRUISubsystem;

/**
 * One inventory / hotbar / equipment / spell slot (docs/adr/0009-user-interface.md): the sunk
 * stone square, the icon, the count, the key, hover and selection, a cooldown sweep. Clicks go to
 * UMRUISubsystem, which applies Minecraft's rules through the inventory source.
 */
class UNREALMERIDIAN_API SMRSlot : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMRSlot) : _Size(22.f), _bSelectable(false), _bToolTip(true), _bHud(false) {}
		/** Size in original pixels. */
		SLATE_ARGUMENT(float, Size)
		/** Small key label in the corner ("1", "7"...). */
		SLATE_ARGUMENT(FString, KeyLabel)
		/** Draw the selection frame when this is the selected hotbar slot. */
		SLATE_ARGUMENT(bool, bSelectable)
		/** Faint icon of what goes here when empty (equipment). */
		SLATE_ARGUMENT(FName, HintIcon)
		/** Show the content's tooltip (off inside a list row, which shows it for the whole row). */
		SLATE_ARGUMENT(bool, bToolTip)
		/** The HUD's look (ui_style.json "hud": Shards' sunk box with a gold edge); Size is then in HUD pixels. */
		SLATE_ARGUMENT(bool, bHud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI, const FMRSlotRef& InSlot);

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual void OnMouseEnter(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override;
	virtual TSharedPtr<IToolTip> GetToolTip() override;

	const FMRSlotRef& GetSlot() const { return SlotRef; }

private:
	TWeakObjectPtr<UMRUISubsystem> UI;
	FMRSlotRef SlotRef;
	float SizePx = 22.f;
	FString KeyLabel;
	bool bSelectable = false;
	bool bToolTip = true;
	bool bHud = false;
	FName HintIcon;
	FName ToolTipFor;
	TSharedPtr<IToolTip> CachedToolTip;

	int32 PaintHud(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& WStyle) const;
};

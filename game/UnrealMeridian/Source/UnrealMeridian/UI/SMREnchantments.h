#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UMRUISubsystem;

/**
 * The enchantments on the player or on the room, as small icons (merintr enchant.c: the player's
 * by their portrait, the room's by the view). Online only: the server's BP_ADD_ENCHANTMENT /
 * BP_REMOVE_ENCHANTMENT (UMRNetSubsystem). Hovering one names it.
 */
class UNREALMERIDIAN_API SMREnchantments : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMREnchantments) : _bRoom(false), _IconPx(14.f), _bHud(false), _WrapWidth(0.f), _bCentre(false) {}
		/** The room's enchantments, else the player's. */
		SLATE_ARGUMENT(bool, bRoom)
		/** One icon's size, in original pixels (HUD pixels with bHud). */
		SLATE_ARGUMENT(float, IconPx)
		/** The HUD's look (Shards' .modern-enchantments): each icon in a small panel, 3 px apart. */
		SLATE_ARGUMENT(bool, bHud)
		/** Wrap onto more rows past this width (HUD pixels; 0: one row). */
		SLATE_ARGUMENT(float, WrapWidth)
		/** Centre each row (under the map). */
		SLATE_ARGUMENT(bool, bCentre)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override;

private:
	const TArray<struct FMRNetObject>* List() const;
	/** Where icon i goes (Slate units), and the whole size. */
	FVector2f IconAt(int32 i, int32 N, float RowWidth) const;
	int32 PerRow(int32 N) const;
	int32 IconUnder(const FGeometry& Geo, const FPointerEvent& Event) const;
	float Unit() const;
	float Gap() const;
	TWeakObjectPtr<UMRUISubsystem> UI;
	bool bRoom = false;
	float IconPx = 14.f;
	bool bHud = false;
	float WrapWidth = 0.f;
	bool bCentre = false;
};

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
	SLATE_BEGIN_ARGS(SMREnchantments) : _bRoom(false), _IconPx(14.f) {}
		/** The room's enchantments, else the player's. */
		SLATE_ARGUMENT(bool, bRoom)
		/** One icon's size, in original pixels. */
		SLATE_ARGUMENT(float, IconPx)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override;

private:
	const TArray<struct FMRNetObject>* List() const;
	TWeakObjectPtr<UMRUISubsystem> UI;
	bool bRoom = false;
	float IconPx = 14.f;
};

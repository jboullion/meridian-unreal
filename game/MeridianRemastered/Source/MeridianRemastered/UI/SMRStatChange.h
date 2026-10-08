#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UMRUISubsystem;
struct FMRNetStatChange;

/**
 * Retraining (docs/adr/0012 M5): a town elder's offer (BP_STAT_CHANGE) to move points between the six
 * stats. Each stays within 1..50 and the total stays as offered; the school levels are kept as they
 * are (the original module also lowers them for points: not here yet). Change sends
 * BP_CHANGED_STATS; the server answers OK or not (UMRUISubsystem closes the dialog).
 */
class MERIDIANREMASTERED_API SMRStatChange : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMRStatChange) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMRUISubsystem* InUI);
	/** Start from the server's offer. */
	void Reset(const FMRNetStatChange& Offer);
	/** Points not given to a stat yet. */
	int32 PointsLeft() const;
	/** The six stats as they stand (might, intellect, stamina, agility, mysticism, aim). */
	const int32* GetValues() const { return Values; }

private:
	void Rebuild();
	void Step(int32 Stat, int32 Delta);
	TWeakObjectPtr<UMRUISubsystem> UI;
	int32 Values[6] = {};
	int32 Total = 0;
};

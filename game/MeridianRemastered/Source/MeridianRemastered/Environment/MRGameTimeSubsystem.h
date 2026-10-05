#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRGameTimeSubsystem.generated.h"

class UMaterialParameterCollection;

/**
 * Meridian game time, as the original server keeps it (kod util/system.kod SystemInitGameHour):
 * a game day lasts two real hours, so a game hour is five real minutes, and the hour follows from
 * UTC alone - ((UTC - 5 h) mod 2 h) / 5 min - so every client agrees without asking the server.
 *
 * Each tick it writes the hour (fractional, 0..24) to MPC_Environment.GameHour, which the clock
 * faces read to show the hour the original server would (frame hour mod 12, kod raza.kod).
 * -MRGameHour=<h> on the command line, or the console variable mr.GameHour (>= 0), pins it (look-dev).
 */
UCLASS()
class MERIDIANREMASTERED_API UMRGameTimeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Game hour (0..24, fractional) for a moment in UTC. */
	static double GameHourAt(const FDateTime& Utc);

	/** The current game hour (0..24, fractional), or the pinned one. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Time")
	double GetGameHour() const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	double PinnedHour = -1.0;
	double LastWritten = -1.0;
};

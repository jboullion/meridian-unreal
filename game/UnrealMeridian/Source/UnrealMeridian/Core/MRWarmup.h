#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MRWarmup.generated.h"

DECLARE_MULTICAST_DELEGATE(FMRWarmupReady);

/**
 * The warm-up behind the loading screen (SMRLoadingScreen, docs/performance.md): every zone is loaded
 * at once (UMRZoneSubsystem::LoadAllZoneLevels), and what the world needs to draw without hitches is
 * still being made afterwards: shaders (an uncooked game compiles the ones missing from the DDC),
 * the sprite atlases (loaded here, which would otherwise stop the frame the first time a look is drawn),
 * meshes, distance fields and textures (uncooked: built or fetched from the DDC), the pipeline states
 * the engine precaches for every component as it registers, and the textures streaming in.
 *
 * Polls those counts every frame and is Ready once they have all been zero for a moment
 * (or after mr.Warmup.MaxSeconds). mr.Warmup 0 and the test modes skip it (Ready at once).
 */
UCLASS()
class UNREALMERIDIAN_API UMRWarmup : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Whether this run shows the warm-up at all (a rendering game, not a test mode, mr.Warmup 1). */
	static bool IsWanted();

	bool IsReady() const { return bReady; }
	/** 0..1, never going back. */
	float GetProgress() const { return Progress; }
	/** What is being waited for, for the loading screen. */
	FText GetStatus() const;
	/** Seconds from the start of play until Ready (-1 until then). */
	double GetReadySeconds() const { return ReadySeconds; }

	/** Broadcast once, when Ready (on the game thread, outside Slate's tick). */
	FMRWarmupReady OnReady;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FCounts
	{
		int32 Shaders = 0;   // shader compile jobs (uncooked)
		int32 Assets = 0;    // meshes, textures, distance fields and the like still building (uncooked)
		int32 Pipelines = 0; // pipeline states being precached
		int32 Sprites = 0;   // sprite atlases still loading (PreloadSprites)
		int32 Textures = 0;  // textures wanting to stream in
		int32 Total() const { return Shaders + Assets + Pipelines + Sprites + Textures; }
	};
	FCounts Poll() const;
	void PreloadSprites();
	void Finish(const TCHAR* Why);

	/** Holds the sprite atlases loaded (PreloadSprites). */
	TSharedPtr<struct FStreamableHandle> SpriteHandle;
	FCounts Last;
	int32 MostSeen = 0;
	double StartTime = 0.0;
	double QuietSince = -1.0;
	double ReadySeconds = -1.0;
	float Progress = 0.f;
	bool bStarted = false;
	bool bReady = false;
};

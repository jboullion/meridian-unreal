#include "Core/MRWarmup.h"

#include "Character/MRSpriteData.h"
#include "ContentStreaming.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "PipelineStateCache.h"
#include "Tests/MRLookDevTour.h"
#include "Tests/MRMapCapture.h"
#include "Tests/MRMonsterTour.h"
#include "Tests/MRMoveTest.h"
#include "Tests/MRNetTest.h"
#include "Tests/MRProfileTour.h"
#include "Tests/MRScreenshotTour.h"
#include "Tests/MRSpriteClipTour.h"
#include "Tests/MRStepSurvey.h"
#include "Tests/MRUIShots.h"
#include "UnrealMeridian.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

#define LOCTEXT_NAMESPACE "MRWarmup"

namespace
{
	TAutoConsoleVariable<int32> CVarWarmup(
		TEXT("mr.Warmup"), 1,
		TEXT("1: hold the game behind the loading screen until shaders, pipeline states, meshes and textures are ready.\n")
		TEXT("0: start at once (what is missing is made while playing, with hitches)."));
	TAutoConsoleVariable<float> CVarMaxSeconds(
		TEXT("mr.Warmup.MaxSeconds"), 0.f,
		TEXT("Give up waiting after this long (s). 0: 120 in an uncooked game (shaders may compile), 30 in a packaged one."));
	TAutoConsoleVariable<float> CVarTextureSeconds(
		TEXT("mr.Warmup.TextureSeconds"), 8.f,
		TEXT("Wait at most this long (s) for textures to stream in once everything else is ready."));

	constexpr double MinSeconds = 1.0;    // the screen never just flashes
	constexpr double QuietSeconds = 0.5;  // every count at zero this long: ready
}

bool UMRWarmup::IsWanted()
{
	if (!FApp::CanEverRender() || CVarWarmup.GetValueOnGameThread() == 0)
	{
		return false;
	}
	// the test modes time their own captures (the hitch tour waits for the warm-up like a player)
	return !(UMRScreenshotTour::IsRequested() || UMRProfileTour::IsRequested() || UMRLookDevTour::IsRequested()
		|| UMRMapCapture::IsRequested() || UMRMonsterTour::IsRequested() || UMRSpriteClipTour::IsRequested()
		|| UMRUIShots::IsRequested() || UMRMoveTest::IsRequested() || UMRStepSurvey::IsRequested()
		|| UMRNetTest::IsRequested());
}

bool UMRWarmup::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer();
}

bool UMRWarmup::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMRWarmup::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StartTime = FPlatformTime::Seconds();
	bStarted = true;
	if (!IsWanted())
	{
		Finish(TEXT("skipped"));
		return;
	}
	PreloadSprites();
}

void UMRWarmup::PreloadSprites()
{
	// the sprite atlases (UMRSpriteBodyComponent) are loaded the first time a look is drawn, and a load
	// then stops the frame (FlushAsyncLoading): load them all now, behind the loading screen, and keep
	// them (~40 MB)
	TArray<FSoftObjectPath> Paths;
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	for (const TPair<FString, FMRSpriteAtlas>& Pair : Lib.Atlases)
	{
		for (const FString& Name : {Pair.Value.Texture, Pair.Value.RampTexture})
		{
			if (!Name.IsEmpty())
			{
				Paths.AddUnique(FSoftObjectPath(FString::Printf(TEXT("%s/%s.%s"), *Lib.TextureDir, *Name, *Name)));
			}
		}
	}
	if (Paths.Num() > 0)
	{
		SpriteHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate(),
			FStreamableManager::AsyncLoadHighPriority);
	}
}

TStatId UMRWarmup::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRWarmup, STATGROUP_Tickables);
}

UMRWarmup::FCounts UMRWarmup::Poll() const
{
	FCounts C;
#if WITH_EDITOR
	// an uncooked game (npm run play) builds what the DDC doesn't have: shaders, and meshes, textures
	// and distance fields (all of which the asset compiling manager counts, the shaders included)
	if (GShaderCompilingManager)
	{
		C.Shaders = GShaderCompilingManager->GetNumRemainingJobs();
	}
	C.Assets = FMath::Max(0, FAssetCompilingManager::Get().GetNumRemainingAssets() - C.Shaders);
#endif
	C.Pipelines = int32(PipelineStateCache::NumActivePrecacheRequests());
	if (SpriteHandle.IsValid() && SpriteHandle->IsLoadingInProgress())
	{
		int32 Loaded = 0, Requested = 0;
		SpriteHandle->GetLoadedCount(Loaded, Requested);
		C.Sprites = FMath::Max(0, Requested - Loaded);
	}
	C.Textures = IStreamingManager::Get().GetNumWantingResources();
	return C;
}

void UMRWarmup::Tick(float DeltaTime)
{
	if (!bStarted || bReady)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const double Elapsed = Now - StartTime;
	Last = Poll();

	// textures stream for as long as the view moves: only wait for them a little once the rest is done
	FCounts Blocking = Last;
	const bool bRestDone = Last.Shaders + Last.Assets + Last.Pipelines + Last.Sprites == 0;
	if (bRestDone && QuietSince < 0.0)
	{
		QuietSince = Now;
	}
	else if (!bRestDone)
	{
		QuietSince = -1.0;
	}
	if (bRestDone && Now - QuietSince > CVarTextureSeconds.GetValueOnGameThread())
	{
		Blocking.Textures = 0;
	}

	MostSeen = FMath::Max(MostSeen, Blocking.Total());
	if (MostSeen > 0)
	{
		Progress = FMath::Max(Progress, 1.f - float(Blocking.Total()) / float(MostSeen));
	}

	float MaxSeconds = CVarMaxSeconds.GetValueOnGameThread();
	if (MaxSeconds <= 0.f)
	{
		MaxSeconds = WITH_EDITOR ? 120.f : 30.f;
	}
	if (Elapsed >= MinSeconds && Blocking.Total() == 0 && QuietSince >= 0.0 && Now - QuietSince >= QuietSeconds)
	{
		Finish(TEXT("ready"));
	}
	else if (Elapsed >= MaxSeconds)
	{
		Finish(TEXT("gave up waiting"));
	}
}

void UMRWarmup::Finish(const TCHAR* Why)
{
	bReady = true;
	Progress = 1.f;
	ReadySeconds = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogMeridian, Display, TEXT("MRWarmup: %s after %.1f s (left: shaders %d, assets %d, pipelines %d, sprites %d, textures %d; most %d)"),
		Why, ReadySeconds, Last.Shaders, Last.Assets, Last.Pipelines, Last.Sprites, Last.Textures, MostSeen);
	OnReady.Broadcast();
}

FText UMRWarmup::GetStatus() const
{
	if (bReady)
	{
		return LOCTEXT("Ready", "Ready");
	}
	if (Last.Shaders > 0)
	{
		return FText::Format(LOCTEXT("Shaders", "Compiling shaders ({0} left)"), FText::AsNumber(Last.Shaders));
	}
	if (Last.Assets > 0)
	{
		return FText::Format(LOCTEXT("Assets", "Building meshes and textures ({0} left)"), FText::AsNumber(Last.Assets));
	}
	if (Last.Sprites > 0)
	{
		return FText::Format(LOCTEXT("Sprites", "Loading characters ({0} left)"), FText::AsNumber(Last.Sprites));
	}
	if (Last.Pipelines > 0)
	{
		return FText::Format(LOCTEXT("Pipelines", "Preparing materials ({0} left)"), FText::AsNumber(Last.Pipelines));
	}
	if (Last.Textures > 0)
	{
		return LOCTEXT("Textures", "Streaming textures");
	}
	return LOCTEXT("Starting", "Preparing the world");
}

#undef LOCTEXT_NAMESPACE

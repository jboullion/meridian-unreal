#include "Character/MRSpriteTargetPool.h"

#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPoolMB(TEXT("mr.Sprite.PoolMB"), 96,
		TEXT("How much video memory (MB) of free sprite render targets is kept for reuse (UMRSpriteTargetPool). 0 keeps none."));
	TAutoConsoleVariable<int32> CVarResizesPerFrame(TEXT("mr.Sprite.ResizesPerFrame"), 2,
		TEXT("How many sprites may change their render targets' size for the camera's distance in one frame. 0: no limit."));

	UTextureRenderTarget2D* MakeTarget(UObject* Outer, FIntPoint Size, bool bColour)
	{
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Outer);
		RT->ClearColor = FLinearColor::Transparent;
		RT->TargetGamma = 1.f;
		RT->AddressX = TA_Clamp;
		RT->AddressY = TA_Clamp;
		if (bColour)
		{
			RT->RenderTargetFormat = RTF_RGBA8_SRGB;  // the canvas writes linear colour; the sRGB target encodes it
			RT->InitAutoFormat(Size.X, Size.Y);
		}
		else
		{
			// exact values (ids, ramps), so linear and unfiltered
			RT->RenderTargetFormat = RTF_RGBA8;
			RT->bForceLinearGamma = true;
			RT->Filter = TF_Nearest;
			RT->InitCustomFormat(Size.X, Size.Y, PF_B8G8R8A8, true);
		}
		RT->UpdateResourceImmediate(true);
		return RT;
	}
}

FIntPoint FMRSpriteTargets::Size() const
{
	return Colour ? FIntPoint(Colour->SizeX, Colour->SizeY) : FIntPoint::ZeroValue;
}

int64 FMRSpriteTargets::Bytes() const
{
	const FIntPoint S = Size();
	return int64(S.X) * S.Y * 4 * 3;
}

FMRSpriteTargets UMRSpriteTargetPool::Make(UObject* Outer, FIntPoint Size)
{
	FMRSpriteTargets T;
	T.Colour = MakeTarget(Outer, Size, true);
	T.Code = MakeTarget(Outer, Size, false);
	T.Ramp = MakeTarget(Outer, Size, false);
	return T;
}

FMRSpriteTargets UMRSpriteTargetPool::Acquire(FIntPoint Size)
{
	Size = FIntPoint(FMath::Max(1, Size.X), FMath::Max(1, Size.Y));
	for (int32 i = Free.Num() - 1; i >= 0; --i)
	{
		if (Free[i].IsValid() && Free[i].Size() == Size)
		{
			const FMRSpriteTargets T = Free[i];
			FreeBytes -= T.Bytes();
			Free.RemoveAt(i);
			return T;
		}
	}
	return Make(this, Size);
}

void UMRSpriteTargetPool::Release(const FMRSpriteTargets& Targets)
{
	if (!Targets.IsValid())
	{
		return;
	}
	Free.Add(Targets);
	FreeBytes += Targets.Bytes();
	// the longest unused go first
	const int64 Cap = int64(FMath::Max(0, CVarPoolMB.GetValueOnGameThread())) * 1024 * 1024;
	while (FreeBytes > Cap && Free.Num() > 0)
	{
		FreeBytes -= Free[0].Bytes();
		Free.RemoveAt(0);
	}
}

bool UMRSpriteTargetPool::TryResize()
{
	const int32 Max = CVarResizesPerFrame.GetValueOnGameThread();
	if (ResizeFrame != GFrameCounter)
	{
		ResizeFrame = GFrameCounter;
		ResizesThisFrame = 0;
	}
	if (Max > 0 && ResizesThisFrame >= Max)
	{
		return false;
	}
	++ResizesThisFrame;
	return true;
}

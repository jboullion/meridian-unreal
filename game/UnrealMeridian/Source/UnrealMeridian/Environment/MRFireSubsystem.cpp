#include "Environment/MRFireSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "Environment/MRFireActor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UnrealMeridian.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFlicker(
		TEXT("mr.Fire.Flicker"), 1, TEXT("0: fire lights hold steady (comparisons)."));
	TAutoConsoleVariable<float> CVarAmplitude(
		TEXT("mr.Fire.Amplitude"), 0.16f,
		TEXT("How far a fire light's intensity swings either way (0.16: the original's +-40 of 255)."));
	TAutoConsoleVariable<float> CVarJitterCm(
		TEXT("mr.Fire.JitterCm"), 1.5f, TEXT("How far a fire light's source wanders (cm); not the shadow-casting ones."));
	TAutoConsoleVariable<float> CVarFlickerDistance(
		TEXT("mr.Fire.FlickerDistanceM"), 30.f, TEXT("Fire lights farther than this (m) from the view hold steady."));
	TAutoConsoleVariable<float> CVarCullDistance(
		TEXT("mr.Fire.CullDistanceM"), 60.f, TEXT("Fire lights farther than this (m) from the view are off."));
	TAutoConsoleVariable<int32> CVarShadowBudget(
		TEXT("mr.Fire.ShadowBudget"), 4, TEXT("How many of the nearest eligible fire lights cast shadows."));

	/** -1..1 for an integer step and a seed. */
	float StepNoise(int64 Step, int32 Seed)
	{
		uint32 X = uint32(Step) * 0x9E3779B1u ^ uint32(Seed) * 0x85EBCA77u;
		X ^= X >> 15;
		X *= 0x2C1B3C6Du;
		X ^= X >> 12;
		X *= 0x297A2D39u;
		X ^= X >> 15;
		return float(X & 0xFFFFFF) / float(0x7FFFFF) - 1.f;
	}
}

float UMRFireSubsystem::FlickerAt(double Seconds, int32 Seed)
{
	// slow breathing (three sines at seeded phases and rates) under a 10 Hz random step, smoothed:
	// the original redrew flickering light every 100 ms (clientd3d roomanim.h FLICKER_PERIOD)
	const FRandomStream Stream(Seed);
	const double Rate = 1.0 + Stream.FRandRange(-0.15, 0.15);
	double Waves = 0.0;
	const double Freq[3] = {1.3, 2.9, 7.1};
	const double Weight[3] = {0.5, 0.3, 0.2};
	for (int32 i = 0; i < 3; ++i)
	{
		Waves += Weight[i] * FMath::Sin(UE_TWO_PI * Freq[i] * Rate * Seconds + Stream.FRandRange(0.0, UE_TWO_PI));
	}
	const double T = Seconds * 10.0;
	const int64 Step = FMath::FloorToInt64(T);
	const float A = FMath::SmoothStep(0.f, 1.f, float(T - double(Step)));
	const float Steps = FMath::Lerp(StepNoise(Step, Seed), StepNoise(Step + 1, Seed), A);
	return FMath::Clamp(float(0.6 * Waves) + 0.4f * Steps, -1.f, 1.f);
}

bool UMRFireSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UMRFireSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMRFireSubsystem::Register(AMRFireActor* Fire)
{
	if (Fire && Fire->HasLight())
	{
		Fires.AddUnique(Fire);
	}
}

void UMRFireSubsystem::Unregister(AMRFireActor* Fire)
{
	Fires.Remove(Fire);
	Shadowed.Remove(Fire);
}

bool UMRFireSubsystem::ViewLocation(FVector& Out) const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return false;
	}
	Out = PC->PlayerCameraManager->GetCameraLocation();
	return true;
}

void UMRFireSubsystem::Tick(float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(MRFireTick);
	Fires.RemoveAll([](const TWeakObjectPtr<AMRFireActor>& F) { return !F.IsValid(); });
	if (Fires.Num() != LoggedFires)
	{
		UE_LOG(LogMeridian, Log, TEXT("Fire: %d fire lights in the loaded zones"), Fires.Num());
		LoggedFires = Fires.Num();
	}
	FVector View;
	if (Fires.Num() == 0 || !ViewLocation(View))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bFlicker = CVarFlicker.GetValueOnGameThread() != 0;
	const float Amplitude = CVarAmplitude.GetValueOnGameThread();
	const float Jitter = CVarJitterCm.GetValueOnGameThread();
	const double FlickerDist = CVarFlickerDistance.GetValueOnGameThread() * 100.0;
	const double CullDist = CVarCullDistance.GetValueOnGameThread() * 100.0;
	const bool bPickShadows = Now >= NextShadowPick;
	TArray<TPair<double, AMRFireActor*>> Candidates;

	for (const TWeakObjectPtr<AMRFireActor>& Weak : Fires)
	{
		AMRFireActor* Fire = Weak.Get();
		UPointLightComponent* Light = Fire->GetLight();
		const double Dist = FVector::Dist(Fire->GetActorLocation() + Fire->BaseLightOffset, View);
		const bool bOn = Dist < CullDist;
		if (Light->IsVisible() != bOn)
		{
			Light->SetVisibility(bOn);
		}
		if (!bOn)
		{
			continue;
		}
		if (bFlicker && Fire->bFlicker && Dist < FlickerDist)
		{
			const float F = FlickerAt(Now, Fire->Seed);
			const float M = 1.f + Amplitude * F;
			Light->SetIntensity(Fire->BaseIntensity * M);
			// a dimmer flame is a redder one
			const float Dim = FMath::Min(0.f, M - 1.f);
			const FLinearColor C = Fire->BaseColor;
			Light->SetLightColor(FLinearColor(C.R, C.G * (1.f + 0.5f * Dim), C.B * (1.f + 1.2f * Dim)));
			// a shadow-casting light that moves re-renders its shadow every frame: those hold still
			if (Jitter > 0.f && !Light->CastShadows)
			{
				const FVector Wander(FlickerAt(Now * 0.7, Fire->Seed + 101), FlickerAt(Now * 0.7, Fire->Seed + 202),
					FlickerAt(Now * 0.7, Fire->Seed + 303));
				Light->SetRelativeLocation(Fire->BaseLightOffset + Wander * Jitter);
			}
		}
		else if (Light->Intensity != Fire->BaseIntensity)
		{
			Light->SetIntensity(Fire->BaseIntensity);
			Light->SetLightColor(Fire->BaseColor);
			Light->SetRelativeLocation(Fire->BaseLightOffset);
		}
		if (bPickShadows && Fire->bShadowEligible)
		{
			Candidates.Emplace(Dist, Fire);
		}
	}

	if (bPickShadows)
	{
		NextShadowPick = Now + 0.25;
		Candidates.Sort([](const TPair<double, AMRFireActor*>& A, const TPair<double, AMRFireActor*>& B) { return A.Key < B.Key; });
		TSet<TWeakObjectPtr<AMRFireActor>> Want;
		const int32 Budget = FMath::Max(0, CVarShadowBudget.GetValueOnGameThread());
		for (int32 i = 0; i < FMath::Min(Budget, Candidates.Num()); ++i)
		{
			Want.Add(Candidates[i].Value);
		}
		for (const TWeakObjectPtr<AMRFireActor>& Weak : Fires)
		{
			UPointLightComponent* Light = Weak->GetLight();
			const bool bShadow = Want.Contains(Weak);
			if (Light->CastShadows != bShadow)
			{
				Light->SetCastShadows(bShadow);
			}
		}
		Shadowed = MoveTemp(Want);
	}
}

TStatId UMRFireSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMRFireSubsystem, STATGROUP_Tickables);
}

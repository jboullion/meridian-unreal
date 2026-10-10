#include "Environment/MRScatterActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace
{
	void ApplyToAll(IConsoleVariable*);

	TAutoConsoleVariable<float> CVarDensity(
		TEXT("mr.Grass.Density"), 1.f,
		TEXT("Share of the scattered grass drawn (0..1): the world build places it all, the game keeps this share."),
		FConsoleVariableDelegate::CreateStatic(&ApplyToAll));
	TAutoConsoleVariable<int32> CVarShadows(
		TEXT("mr.Grass.Shadows"), 1,
		TEXT("0: the grass casts no shadows (1: as the zone's scatter rule says)."),
		FConsoleVariableDelegate::CreateStatic(&ApplyToAll));
	TAutoConsoleVariable<float> CVarDistance(
		TEXT("mr.Grass.Distance"), 1.f,
		TEXT("Scales how far the grass is drawn (its rule's cull_m)."),
		FConsoleVariableDelegate::CreateStatic(&ApplyToAll));

	void ApplyToAll(IConsoleVariable*)
	{
		for (const FWorldContext& Context : GEngine ? GEngine->GetWorldContexts() : TIndirectArray<FWorldContext>())
		{
			if (UWorld* World = Context.World(); World && World->IsGameWorld())
			{
				for (TActorIterator<AMRScatterActor> It(World); It; ++It)
				{
					It->ApplySettings();
				}
			}
		}
	}

	/** 0..1 per instance index, the same every run (a thinned field keeps the same tufts). */
	float Keep(int32 Index)
	{
		uint32 X = uint32(Index) * 0x9E3779B1u;
		X ^= X >> 16;
		X *= 0x85EBCA6Bu;
		X ^= X >> 13;
		return float(X & 0xFFFF) / 65535.f;
	}
}

AMRScatterActor::AMRScatterActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	Instances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Instances"));
	Instances->SetMobility(EComponentMobility::Static);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Instances->SetGenerateOverlapEvents(false);
	Instances->bAffectDistanceFieldLighting = false;
	Instances->bAffectDynamicIndirectLighting = false;
	RootComponent = Instances;
}

void AMRScatterActor::SetScatter(UStaticMesh* Mesh, const TArray<FTransform>& Transforms, float CullStartCm, float CullEndCm, bool bCastShadow)
{
	Instances->ClearInstances();
	Instances->SetStaticMesh(Mesh);
	Instances->SetCullDistances(FMath::RoundToInt(CullStartCm), FMath::RoundToInt(CullEndCm));
	Instances->SetCastShadow(bCastShadow);
	Instances->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);
}

int32 AMRScatterActor::GetInstanceCount() const
{
	return Instances->GetInstanceCount();
}

void AMRScatterActor::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		ApplySettings();
	}
}

void AMRScatterActor::ApplySettings()
{
	if (!bCaptured)
	{
		// what the world build placed, kept so the settings can go back up
		bCaptured = true;
		BuiltShadow = Instances->CastShadow;
		BuiltCullStart = Instances->InstanceStartCullDistance;
		BuiltCullEnd = Instances->InstanceEndCullDistance;
	}
	const float Distance = FMath::Max(0.05f, CVarDistance.GetValueOnGameThread());
	Instances->SetCullDistances(FMath::RoundToInt(BuiltCullStart * Distance), FMath::RoundToInt(BuiltCullEnd * Distance));
	const bool bShadow = BuiltShadow && CVarShadows.GetValueOnGameThread() != 0;
	if (Instances->CastShadow != bShadow)
	{
		Instances->SetCastShadow(bShadow);
	}

	const float Density = FMath::Clamp(CVarDensity.GetValueOnGameThread(), 0.f, 1.f);
	if (Density == AppliedDensity)
	{
		return;
	}
	if (BuiltTransforms.Num() == 0 && Density < 1.f)
	{
		BuiltTransforms.Reserve(Instances->GetInstanceCount());
		for (int32 i = 0; i < Instances->GetInstanceCount(); ++i)
		{
			FTransform T;
			Instances->GetInstanceTransform(i, T, /*bWorldSpace*/ false);
			BuiltTransforms.Add(T);
		}
	}
	if (BuiltTransforms.Num() > 0)
	{
		TArray<FTransform> Kept;
		Kept.Reserve(BuiltTransforms.Num());
		for (int32 i = 0; i < BuiltTransforms.Num(); ++i)
		{
			if (Keep(i) < Density)
			{
				Kept.Add(BuiltTransforms[i]);
			}
		}
		Instances->ClearInstances();
		Instances->AddInstances(Kept, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);
	}
	AppliedDensity = Density;
}

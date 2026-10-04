#include "Environment/MRScatterActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

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

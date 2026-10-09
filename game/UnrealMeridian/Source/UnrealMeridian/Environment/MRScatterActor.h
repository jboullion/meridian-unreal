#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRScatterActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * Many copies of one small mesh (grass clumps, pebbles...) placed by the world build
 * (tools/ue/build_world.py, docs/adr/0003 ground pass). Client-side decoration only: no collision,
 * not replicated, culled beyond a distance.
 */
UCLASS()
class UNREALMERIDIAN_API AMRScatterActor : public AActor
{
	GENERATED_BODY()

public:
	AMRScatterActor();

	/** Editor scripting: replace the instances. Transforms are relative to the actor. */
	UFUNCTION(BlueprintCallable, Category = "Meridian|Scatter")
	void SetScatter(UStaticMesh* Mesh, const TArray<FTransform>& Transforms, float CullStartCm, float CullEndCm, bool bCastShadow);

	UFUNCTION(BlueprintCallable, Category = "Meridian|Scatter")
	int32 GetInstanceCount() const;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Instances;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRRuntimeRoom.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
class UTexture2D;
struct FMRRoomMesh;

/**
 * A room built at runtime from the server's .roo (docs/adr/0012-client-parity-and-world-coverage.md):
 * the original's textures through its palette, lit by its sector lights (M_RuntimeRoom, unlit), and
 * a hidden collision mesh on WorldStatic, so floor traces and the capsule work as on a built zone.
 * UMRRuntimeRooms makes and keeps them.
 */
UCLASS(NotPlaceable)
class MERIDIANREMASTERED_API AMRRuntimeRoom : public AActor
{
	GENERATED_BODY()

public:
	AMRRuntimeRoom();

	/** Textures by number (missing ones draw a grey checker); Material has a "Tex" texture parameter. */
	void Build(const FMRRoomMesh& RenderMesh, const FMRRoomMesh& CollisionMesh, const TMap<uint16, UTexture2D*>& InTextures,
		UMaterialInterface* Material);

	/** /Game/Generated/Runtime/M_RuntimeRoom (tools/ue/environment_materials.py), or null if it hasn't been built. */
	static UMaterialInterface* LoadMaterial();

	const FString& GetRoomFile() const { return RoomFile; }
	void SetRoomFile(const FString& InRoomFile) { RoomFile = InRoomFile; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> RenderMeshComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> CollisionMeshComponent;

	/** Kept alive with the room. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Textures;

	FString RoomFile;
};

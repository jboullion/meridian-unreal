#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/MRRooFile.h"
#include "World/MRRoomMesh.h"
#include "MRRuntimeRoom.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTexture2D;
struct FMRRoomMesh;

/**
 * A room built at runtime from the server's .roo (docs/adr/0012-client-parity-and-world-coverage.md):
 * the original's textures through its palette, lit by its sector lights (M_RuntimeRoom, unlit), and
 * a hidden collision mesh on WorldStatic, so floor traces and the capsule work as on a built zone.
 * UMRRuntimeRooms makes and keeps them.
 *
 * It keeps the room it was built from, so the server's changes rebuild it (M7, clientd3d roomanim.c):
 * lifts and doors (BP_SECTOR_MOVE), depth and scrolling (BP_SECTOR_CHANGE), textures (BP_CHANGE_TEXTURE).
 * Each room entry starts again from the file (ResetChanges): the server sends its changes again then.
 */
UCLASS(NotPlaceable)
class UNREALMERIDIAN_API AMRRuntimeRoom : public AActor
{
	GENERATED_BODY()

public:
	AMRRuntimeRoom();

	/**
	 * Build from the room file. Repeats: ROO units a wall texture repeats over (its BGF's size / shrink);
	 * Textures by number (missing ones draw a grey checker); Material has a "Tex" texture parameter.
	 */
	void Build(const FMRRooFile& InRoom, const TMap<uint16, FVector2D>& InRepeats, const TMap<uint16, UTexture2D*>& InTextures,
		UMaterialInterface* Material);

	/** /Game/Generated/Runtime/M_RuntimeRoom (tools/ue/environment_materials.py), or null if it hasn't been built. */
	static UMaterialInterface* LoadMaterial();

	const FString& GetRoomFile() const { return RoomFile; }
	void SetRoomFile(const FString& InRoomFile) { RoomFile = InRoomFile; }

	// ------------------------------------------------------------------ the server's changes

	/** Back to the room as the file has it (a room entry: the server sends the room's changes again). */
	void ResetChanges();
	/** BP_PLAYER's override of the wading depths (FMRWadingOverride): rebuilds the collision when it changes. */
	void SetWadingOverride(const FMRWadingOverride& InWading);
	/** Its wading areas now (zone-local cm): the sectors' depths as changed, and the override. */
	TArray<FMRRoomDepthArea> GetDepthAreas() const { return MRRoomMesh::DepthAreas(Room, Wading); }
	/** Its step walls now (zone-local cm): the floors as moved, and the override. */
	TArray<FMRRoomStepWall> GetStepWalls() const { return MRRoomMesh::StepWalls(Room, Wading); }
	/** After every rebuild of the collision (lifts, depth changes, the override): wading and steps follow it. */
	FSimpleMulticastDelegate OnCollisionRebuilt;
	/** BP_SECTOR_MOVE: the floor (ANIMATE_FLOOR_LIFT) or ceiling of every sector with this id to Height (Kod units) at Speed a second; 0: at once. */
	void MoveSector(uint8 Type, uint16 SectorId, int16 Height, uint8 Speed);
	/** BP_SECTOR_CHANGE: a new depth (0..3) and scroll speed (0..3); CHANGE_OVERRIDE keeps either. */
	void ChangeSector(uint16 SectorId, uint8 Depth, uint8 Scroll);
	/**
	 * BP_CHANGE_TEXTURE: the walls and sectors with this id take Texture where Flags (CTF_*) say.
	 * False when the texture isn't here yet (AddTexture it, it's drawn as a checker meanwhile).
	 */
	bool ChangeTexture(uint16 Id, uint16 Texture, uint8 Flags);
	bool HasTexture(uint16 Texture) const { return TextureByNumber.Contains(Texture); }
	/** A texture fetched after the build (a changed texture's). */
	void AddTexture(uint16 Texture, UTexture2D* Tex, const FVector2D& Repeat);

	const FMRRooFile& GetRoom() const { return Room; }
	/** The drawn mesh's bounds when it was built (room-local cm). */
	const FBox& GetMeshBounds() const { return MeshBounds; }
	/** How many times it's been rebuilt (tests). */
	int32 GetRebuilds() const { return Rebuilds; }
	bool IsMoving() const { return Lifts.Num() > 0; }

	virtual void Tick(float DeltaSeconds) override;

private:
	struct FLift
	{
		int32 Sector = 0;  // index
		bool bCeiling = false;
		double From = 0.0;
		double To = 0.0;
		double Duration = 0.0;
		double Elapsed = 0.0;
	};

	/** The meshes again from Room: the drawn one, and collision too when bCollision. */
	void Rebuild(bool bCollision);
	void SetHeight(int32 Sector, bool bCeiling, int16 Height);
	UMaterialInstanceDynamic* MaterialFor(uint16 Texture);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> RenderMeshComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> CollisionMeshComponent;

	/** Kept alive with the room. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Textures;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	FString RoomFile;
	/** The room as the file has it, and as it is now. */
	FMRRooFile Original;
	FMRRooFile Room;
	TMap<uint16, FVector2D> Repeats;
	TMap<uint16, UTexture2D*> TextureByNumber;
	TMap<uint16, UMaterialInstanceDynamic*> MaterialByTexture;
	TArray<FLift> Lifts;
	FMRWadingOverride Wading;
	bool bChanged = false;
	int32 Rebuilds = 0;
	FBox MeshBounds = FBox(ForceInit);
	double SinceDraw = 0.0;
	double SinceCollision = 0.0;
};

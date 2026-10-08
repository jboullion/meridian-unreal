#pragma once

#include "CoreMinimal.h"

struct FMRRooFile;

/** The triangles of one original texture (0: untextured), in zone-local UE centimetres. */
struct FMRRoomMeshSection
{
	uint16 Texture = 0;
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	/** The sector light (0..1) the face sees, the original's per-sector lighting. */
	TArray<float> Light;
	/** How fast the texture scrolls there (UV units a second; the .roo's scrolling sectors and walls), as UV1. */
	TArray<FVector2D> Scroll;
	TArray<int32> Triangles;
};

struct FMRRoomMesh
{
	/** By texture number, in the order first used. */
	TArray<FMRRoomMeshSection> Sections;

	int32 NumTriangles() const;
	FBox Bounds() const;
};

/** A wading sector's convex piece (zone-local UE cm, XY) and its depth 1..3. */
struct FMRRoomDepthArea
{
	int32 Depth = 0;
	TArray<FVector2D> Points;
};

/**
 * Builds a room's geometry the way tools/roo2gltf/roo2gltf.py does (build_room_mesh, wall_uvs,
 * SectorHeights): this is a C++ port of our own Python, and the two must give the same triangles
 * (Meridian.World.Rooms compares them). Coordinates: UE = (roo_x, roo_y, height) * MRUnits::CmPerRoo.
 */
namespace MRRoomMesh
{
	/** ROO units covered by one repeat of a wall texture (the BGF's size / shrink; 1024 if unknown). */
	using FRepeat = TFunction<FVector2D(uint16 Texture)>;

	/**
	 * bCollision: the surfaces the original moves you on: wading floors lowered to where you stand,
	 * and no middle sections that every sidedef marks passable (field borders, signs, torches).
	 */
	MERIDIANREMASTERED_API FMRRoomMesh Build(const FMRRooFile& Room, const FRepeat& Repeat, bool bCollision);

	/** The sectors with a wading depth, as convex BSP leaves (roo2gltf depth_areas). */
	MERIDIANREMASTERED_API TArray<FMRRoomDepthArea> DepthAreas(const FMRRooFile& Room);
}

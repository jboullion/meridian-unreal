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

/** A wading sector's convex piece (zone-local UE cm, XY), its depth 1..3, and how far below its floor you stand (cm). */
struct FMRRoomDepthArea
{
	int32 Depth = 0;
	double SinkCm = 0.0;
	TArray<FVector2D> Points;
};

/**
 * One way across a wall between two sectors whose floors differ, with what the original's step rule
 * needs there (clientd3d move.c IntersectNode, blakserv roofile.c BSPCanMoveInRoomTreeInternal): from
 * a passable side, a step is never blocked when that side has no lower texture, else when the higher
 * floor at the wall's first end (z1), less the far sector's wading sink, is at most 24 Kod units above
 * where you stand. Zone-local UE cm.
 */
struct FMRRoomStepWall
{
	FVector2D A = FVector2D::ZeroVector, B = FVector2D::ZeroVector;
	/** Across the wall, from the side this way starts on (unit, XY). */
	FVector2D Into = FVector2D::ZeroVector;
	/** The starting side's sidedef has a lower texture: the step is measured. */
	bool bLowerTexture = false;
	/** The higher floor at the wall's first end (the original's z1), cm. */
	double Z1Cm = 0.0;
	/** The far sector's wading sink (sector_depths: the server's override doesn't change the rule), cm. */
	double FarSinkCm = 0.0;
};

/**
 * The server's override of the wading depths (BP_PLAYER's room flags ROOM_OVERRIDE_DEPTH1..3 and its
 * three depths): where a depth is overridden, the original stands you at that height instead of the
 * floor less the depth's sink (clientd3d client3d.c GetFloorBase). Only the Temple of Riija (ke1.roo)
 * uses it: its depth-1 sectors lie on the chasm floor and are walked at 13.2 m, an invisible bridge.
 * Speed still follows the sector's own depth (move.c UserMovePlayer).
 */
struct FMRWadingOverride
{
	/** Per depth 1..3 (index 0 unused): whether it's overridden, and the height you stand at, ROO units. */
	bool bSet[4] = {};
	double FloorRoo[4] = {};

	bool Any() const { return bSet[1] || bSet[2] || bSet[3]; }
	bool operator==(const FMRWadingOverride& O) const
	{
		for (int32 d = 1; d <= 3; ++d)
		{
			if (bSet[d] != O.bSet[d] || (bSet[d] && FloorRoo[d] != O.FloorRoo[d]))
			{
				return false;
			}
		}
		return true;
	}

	/** From BP_PLAYER: room flags (ROOM_OVERRIDE_DEPTH1 = 1, 2 = 2, 3 = 4) and its depths, Kod units (clientd3d server.c: << 4). */
	static FMRWadingOverride FromServer(uint32 RoomFlags, const uint32 (&Depths)[3])
	{
		FMRWadingOverride O;
		for (int32 d = 1; d <= 3; ++d)
		{
			O.bSet[d] = (RoomFlags & (1u << (d - 1))) != 0;
			O.FloorRoo[d] = O.bSet[d] ? static_cast<double>(Depths[d - 1]) * 16.0 : 0.0;
		}
		return O;
	}
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
	 * Wading: the server's overridden depths stand you at their own height instead.
	 */
	UNREALMERIDIAN_API FMRRoomMesh Build(const FMRRooFile& Room, const FRepeat& Repeat, bool bCollision,
		const FMRWadingOverride& Wading = FMRWadingOverride());

	/** Both ways across every passable wall where the collision floors differ (roo2gltf step_walls), as they are now. */
	UNREALMERIDIAN_API TArray<FMRRoomStepWall> StepWalls(const FMRRooFile& Room, const FMRWadingOverride& Wading = FMRWadingOverride());

	/** The sectors with a wading depth, as convex BSP leaves (roo2gltf depth_areas), with the server's overrides. */
	UNREALMERIDIAN_API TArray<FMRRoomDepthArea> DepthAreas(const FMRRooFile& Room, const FMRWadingOverride& Wading = FMRWadingOverride());
}

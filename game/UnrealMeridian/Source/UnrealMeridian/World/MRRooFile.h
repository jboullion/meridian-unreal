#pragma once

#include "CoreMinimal.h"

/**
 * An original room file (.roo, version 15), read as docs/research/roo-format.md describes it.
 * Plain data: positions in ROO units (1024 per square, X east, Y south), heights and texture
 * offsets in Kod fine units (64 per square). Written from our own notes; never port the GPL readers.
 */
namespace MRRoo
{
	constexpr uint32 WF_BACKWARDS = 0x001;
	constexpr uint32 WF_TRANSPARENT = 0x002;
	constexpr uint32 WF_PASSABLE = 0x004;
	constexpr uint32 WF_ABOVE_BOTTOMUP = 0x040;
	constexpr uint32 WF_BELOW_TOPDOWN = 0x080;
	constexpr uint32 WF_NORMAL_TOPDOWN = 0x100;
	constexpr uint32 WF_NO_VTILE = 0x200;

	constexpr uint32 SF_MASK_DEPTH = 0x003;
	constexpr uint32 SF_FLICKER = 0x200;
	constexpr uint32 SF_SLOPED_FLOOR = 0x400;
	constexpr uint32 SF_SLOPED_CEILING = 0x800;

	constexpr double RooPerSquare = 1024.0;
	constexpr double RooPerFine = 16.0;
}

/** A sloped floor or ceiling: the plane a*x + b*y + c*z + d = 0 (ROO units). */
struct FMRRooSlope
{
	float A = 0.f, B = 0.f, C = 1.f, D = 0.f;
	int32 TexX = 0, TexY = 0, TexAngle = 0;
	/** A usable plane (a vertical one can't be a floor: the sector's flat height is used instead). */
	bool IsValid() const { return FMath::Abs(C) > 1e-6f; }
	/** z at (X, Y), ROO units. */
	double HeightAt(double X, double Y) const { return -(A * X + B * Y + D) / C; }
};

struct FMRRooSector
{
	uint16 ServerId = 0;
	uint16 FloorTexture = 0;
	uint16 CeilingTexture = 0;      // 0: open sky
	int16 TexX = 0, TexY = 0;       // fine units
	int16 FloorHeight = 0;          // fine units
	int16 CeilingHeight = 0;
	uint8 Light = 0;
	uint32 Flags = 0;               // SF_*
	uint8 AnimationSpeed = 0;
	bool bSlopedFloor = false;
	bool bSlopedCeiling = false;
	FMRRooSlope FloorSlope;
	FMRRooSlope CeilingSlope;

	int32 Depth() const { return Flags & MRRoo::SF_MASK_DEPTH; }
};

struct FMRRooSidedef
{
	uint16 ServerId = 0;
	uint16 NormalTexture = 0;
	uint16 AboveTexture = 0;
	uint16 BelowTexture = 0;
	uint32 Flags = 0;               // WF_*
	uint8 AnimationSpeed = 0;
};

/** A client wall: sidedefs and sectors are 1-based (0 = none). */
struct FMRRooWall
{
	uint16 Next = 0;
	uint16 PosSidedef = 0, NegSidedef = 0;
	float X0 = 0.f, Y0 = 0.f, X1 = 0.f, Y1 = 0.f;
	float Length = 0.f;
	int16 PosXOffset = 0, NegXOffset = 0, PosYOffset = 0, NegYOffset = 0;  // fine units
	uint16 PosSector = 0, NegSector = 0;
};

/** A BSP node: internal (a separator line and children) or a leaf (a convex polygon of one sector). */
struct FMRRooNode
{
	enum EType : uint8 { Internal = 1, Leaf = 2 };
	uint8 Type = Leaf;
	FBox2f Bounds = FBox2f(ForceInit);
	// internal
	float A = 0.f, B = 0.f, C = 0.f;
	uint16 Pos = 0, Neg = 0, FirstWall = 0;
	// leaf
	uint16 Sector = 0;              // 1-based
	TArray<FVector2f> Points;
};

struct UNREALMERIDIAN_API FMRRooFile
{
	int32 Version = 0;
	uint32 Security = 0;
	/** The Kod grid rectangle [0, Width] x [0, Height], ROO units. */
	int32 Width = 0;
	int32 Height = 0;
	TArray<FMRRooNode> Nodes;
	TArray<FMRRooWall> Walls;
	TArray<FMRRooSidedef> Sidedefs;
	TArray<FMRRooSector> Sectors;

	/** Parse a whole file. False (with Error) when it isn't a room or is cut short. */
	bool Load(const TArray<uint8>& Bytes, FString& OutError);

	/** A sector by its 1-based number, or null. */
	const FMRRooSector* Sector(int32 Num) const { return Sectors.IsValidIndex(Num - 1) ? &Sectors[Num - 1] : nullptr; }
	const FMRRooSidedef* Sidedef(int32 Num) const { return Sidedefs.IsValidIndex(Num - 1) ? &Sidedefs[Num - 1] : nullptr; }

	/** Every texture number the room uses (floors, ceilings, walls), without 0. */
	TSet<uint16> TextureIds() const;
};

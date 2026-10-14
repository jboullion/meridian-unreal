#pragma once

#include "CoreMinimal.h"

/**
 * Where people can walk in a room, for ambient life (docs/adr/0013-ambient-life.md): Meridian Shards' nav
 * grid (packages/world/src/navGrid.ts, ours), read from the .nav files its asset build writes (the
 * server's asset cache online, data/ambient/nav offline) and walked the same way.
 *
 * - Cells are half a square (512 ROO units), centred on the squares' centres and corners, in the room's
 *   ROO coordinates (x east, y south, 1024 a square, from 0).
 * - A cell is walkable on a dry floor with headroom, clear of walls a person can't pass; each links to
 *   its eight neighbours where no such wall lies between (bit d: DIRS[d], E, SE, S, SW, W, NW, N, NE).
 * - Shards checks a straight line against the room's walls; we have no walls here, so a line is clear when
 *   every cell it crosses is open and linked to the last (the links are where the walls aren't).
 */
struct UNREALMERIDIAN_API FMRNavGrid
{
	static constexpr double Cell = 512.0;
	/** navGrid.ts NAV_FORMAT, and the file's magic "NAVG" */
	static constexpr uint32 Format = 2;

	int32 Cols = 0;
	int32 Rows = 0;
	/** The room's security value the grid was built for (the .nav's header) */
	uint32 Security = 0;
	TArray<uint8> Walk;
	TArray<uint8> Links;
	/** Connected area of each walkable cell (two-way links), largest first from 1; 0 for none */
	TArray<uint16> Area;
	/** Closed for now (real objects that block: set by the user) */
	TArray<uint8> Closed;

	/** From a .nav file's bytes (navGrid.ts encodeNavGrid); false if it isn't one of format 2. */
	bool Decode(const TArray<uint8>& Bytes);
	bool IsValid() const { return Cols > 0 && Rows > 0; }
	int32 Num() const { return Cols * Rows; }

	bool IsOpen(int32 I) const { return I >= 0 && I < Num() && Walk[I] && !Closed[I]; }
	FVector2D CellCentre(int32 I) const { return FVector2D((I % Cols) * Cell, (I / Cols) * Cell); }
	/** The cell a point falls in, or -1 off the grid */
	int32 CellAt(double X, double Y) const;
	/** Whether two neighbouring cells are linked (both ways when bBoth) */
	bool Linked(int32 From, int32 To, bool bBoth = false) const;
	/**
	 * Whether a person can walk straight from A to B: every cell on the way open and each linked to the
	 * one before (Shards' walkable, without walls). bOpenStart: the start's own cell needn't be open
	 * (a doorway, a closed cell someone stands in).
	 */
	bool LineClear(const FVector2D& A, const FVector2D& B, bool bOpenStart = true) const;
	/**
	 * The open cell nearest a point within MaxCells, in Area (0: any), reachable from it in a straight
	 * line when bReachable; -1 if none.
	 */
	int32 NearestCell(double X, double Y, int32 MaxCells = 4, int32 InArea = 0, bool bReachable = true) const;
	/** A random open cell in an area (0: any), or -1. Rand gives [0, 1). */
	int32 RandomCell(TFunctionRef<double()> Rand, int32 InArea = 0, int32 Tries = 200) const;
	/** A* from one cell to another: the cells on the way, both ends included; empty if none. */
	TArray<int32> FindCells(int32 From, int32 To, int32 MaxVisits = 40000) const;
	/**
	 * A path between two points, as the points to walk through after From, pulled tight (corners only
	 * where a straight line won't do); false when there's none.
	 */
	bool FindPath(const FVector2D& From, const FVector2D& To, TArray<FVector2D>& Out) const;
	/** Cells in each area (index = area) */
	TArray<int32> AreaSizes() const;

private:
	void LabelAreas();
};

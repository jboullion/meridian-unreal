#pragma once

#include "CoreMinimal.h"

/**
 * Conversions between the original game's coordinates and UE world space.
 * See docs/findings.md and tools/roo2gltf/roo2gltf.py, which must agree with this file.
 *
 * Original:
 *   - ROO units: 1024 per grid square. X grows east, Y grows south (rows).
 *   - Kod fineness: 64 per grid square (positions within a square, sector heights).
 *   - Grid: 1-based row/col, plus 0..63 fine row/col.
 *   - Angles: 0..4096, 0 = east, increasing toward south.
 *
 * UE (left-handed, Z up): X = east, Y = south, Z = up, centimetres.
 * Looking east, +Y is on the right, so ROO (x, y) maps to UE (X, Y) with no mirroring.
 */
namespace MRUnits
{
	/** One original grid square in metres (eye height was 0.75 squares; 1.65 m eye -> 2.2 m). */
	constexpr double MetersPerSquare = 2.2;
	constexpr double RooPerSquare = 1024.0;
	constexpr double KodPerSquare = 64.0;
	constexpr double KodMaxAngle = 4096.0;

	constexpr double CmPerSquare = MetersPerSquare * 100.0;
	constexpr double CmPerRoo = CmPerSquare / RooPerSquare;
	constexpr double CmPerKod = CmPerSquare / KodPerSquare;

	/** Grid (1-based big row/col, 0-based fine) -> ROO units. Mirrors blakserv GRIDCOORDTOROO. */
	inline FVector2D GridToRoo(int32 Row, int32 Col, int32 FineRow = 32, int32 FineCol = 32)
	{
		return FVector2D(((Col - 1) * KodPerSquare + FineCol) * (RooPerSquare / KodPerSquare),
		                 ((Row - 1) * KodPerSquare + FineRow) * (RooPerSquare / KodPerSquare));
	}

	/** ROO (x, y) -> zone-local UE centimetres (Z left at 0). */
	inline FVector RooToLocal(const FVector2D& Roo)
	{
		return FVector(Roo.X * CmPerRoo, Roo.Y * CmPerRoo, 0.0);
	}

	/** Zone-local UE position -> ROO (x, y). */
	inline FVector2D LocalToRoo(const FVector& Local)
	{
		return FVector2D(Local.X / CmPerRoo, Local.Y / CmPerRoo);
	}

	/** Grid -> zone-local UE centimetres (Z left at 0; trace for the floor). */
	inline FVector GridToLocal(int32 Row, int32 Col, int32 FineRow = 32, int32 FineCol = 32)
	{
		return RooToLocal(GridToRoo(Row, Col, FineRow, FineCol));
	}

	/**
	 * Kod angle -> UE yaw in degrees. Kod measures from east toward south. UE yaw measures from
	 * +X (east) toward +Y (south), so the two match directly.
	 */
	inline double KodAngleToYaw(int32 KodAngle)
	{
		return FMath::Fmod(static_cast<double>(KodAngle), KodMaxAngle) * 360.0 / KodMaxAngle;
	}

	/**
	 * zone_layout.json positions are glTF metres [x east, y up, z south] -> zone-local UE cm.
	 */
	inline FVector LayoutToLocal(double GltfX, double GltfY, double GltfZ)
	{
		return FVector(GltfX * 100.0, GltfZ * 100.0, GltfY * 100.0);
	}
}

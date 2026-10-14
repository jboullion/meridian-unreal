#pragma once

#include "CoreMinimal.h"
#include "Ambient/MRAmbientData.h"

class AMRAmbientFigure;
class UWorld;

/**
 * Small critters about the player (docs/adr/0013-ambient-life.md): Meridian Shards' Critters
 * (packages/world/src/ambient/critters.ts, ours). Rats, spiders, centipedes, ants, scorpions and bunnies,
 * the monsters' own art drawn small, with their walk cycles, corpses and death sounds. A click or an attack
 * with nothing real there squishes one: it squeaks, lies a while and fades. The server never hears of them.
 *
 * What lives where comes from the room's terrain (habitatOf). Shards finds floors in the room's sectors;
 * ours traces our collision: a spot is a floor with room above, clear of walls, about the player's level,
 * dry (or shallow water for those that swim: sewers, swamps). They dash a short straight way and rest.
 */
class UNREALMERIDIAN_API FMRCritters
{
public:
	struct FSpecies
	{
		const TCHAR* Key;
		/** Our monster sprite ("GiantRat"), else none: drawn from Icon's bitmap online */
		const TCHAR* Monster;
		const TCHAR* Icon;
		const TCHAR* Dead;
		/** SendMoveAnimation's cycle (groups 1-based) */
		int32 WalkPeriod, WalkLow, WalkHigh;
		/** Drawn this many times the art's size (the monsters are giants of their kind) */
		float Scale;
		/** Squares a second when dashing */
		float Pace;
		/** A dash's length (squares) and a rest's (ms) */
		float DashMin, DashMax, RestMin, RestMax;
		/** The death sound (the monster's sound_death) */
		const TCHAR* Sound;
		/** Out at night this much as by day */
		float Night;
		/** Runs from the player */
		bool bShy;
	};
	struct FHabitat
	{
		FString Name;
		double Count = 0.0;
		TArray<TPair<int32, double>> Mix;  // species index, weight
		bool bWades = false;
	};

	/** The room's critters from its terrain (TERRAIN_* without the prefix), or false for none (critters.ts habitatOf) */
	static bool HabitatOf(const FString& RoomFile, const TArray<FString>& Terrain, FHabitat& Out);
	static const FSpecies& Species(int32 Index);

	FMRCritters(UWorld* InWorld, const FHabitat& InHabitat, uint32 Seed, uint32 InIdBase);
	~FMRCritters();

	struct FFrame
	{
		bool bHasPlayer = false;
		FVector Player = FVector::ZeroVector;
		/** The player's yaw (degrees), or a negative number when not known */
		float FacingYaw = -1.f;
		double Scale = 1.0;
		double Hour = -1.0;
	};
	void Update(double DtMs, const FFrame& Frame);
	/** Squish the nearest live critter within ReachCm of At, ahead of Yaw (within about 60 degrees); true if one died */
	bool SquishAhead(const FVector& At, float Yaw, double ReachCm);
	/** Squish the live critter nearest a ray (a click), within ReachCm of the ray and MaxCm along it */
	bool SquishOnRay(const FVector& Start, const FVector& Dir, double MaxCm, double ReachCm = 45.0);
	int32 NumAlive() const;
	const FHabitat& GetHabitat() const { return Habitat; }

private:
	enum class EMode : uint8 { Rest, Dash, Dead };
	struct FCritter
	{
		uint32 Id = 0;
		int32 Species = 0;
		EMode Mode = EMode::Rest;
		double Timer = 0.0;
		FVector Pos = FVector::ZeroVector;
		FVector To = FVector::ZeroVector;
		float Yaw = 0.f;
		/** cm a millisecond */
		double Pace = 0.0;
		double Fade = 1.0;
		int32 FadeDir = 0;
		TWeakObjectPtr<AMRAmbientFigure> Figure;
		bool bBitmap = false;
	};

	TWeakObjectPtr<UWorld> World;
	FHabitat Habitat;
	MRAmbient::FRand Rand;
	uint32 NextId;
	double Extra = 0.0;
	bool bStarted = false;
	double SpawnMs = 0.0;
	TArray<FCritter> Critters;

	/** A floor a critter can be on at XY near Level (world Z), or false */
	bool Standable(double X, double Y, double Level, double& OutZ) const;
	bool ClearRun(const FVector& From, const FVector& To) const;
	int32 Pick(bool bNight);
	void Spawn(const FFrame& F, bool bNight, bool bFadeIn);
	bool Dash(FCritter& C, const FVector* Away);
	void Step(FCritter& C, double DtMs, const FVector& Player);
	void Show(FCritter& C, bool bNew);
	void Kill(FCritter& C);
	void Remove(FCritter& C);
};

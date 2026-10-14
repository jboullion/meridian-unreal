#pragma once

#include "CoreMinimal.h"
#include "Ambient/MRAmbientData.h"
#include "Ambient/MRNavGrid.h"

/**
 * The townsfolk, inn patrons, courtiers and shop customers of the room you're in (docs/adr/0013-ambient-life.md):
 * Meridian Shards' AmbientLife (packages/world/src/ambient/ambientLife.ts, ours), the same rules in C++.
 * Pure logic in the room's ROO coordinates (x east, y south, 1024 a square); UMRAmbientSubsystem draws it.
 *
 * - In towns, people come out of a doorway and walk to another door (or out of sight) and go in, some
 *   running an errand at a shop's door on the way; guards in the town's colours pace a beat.
 * - In inns, patrons stand about, wander, gather in twos and now and then leave by the door while others
 *   come in. Castles' knights pace and their courtiers stand about; shops get a customer or two.
 * - They step aside for the player, look twice at fine armour, wave back when waved at, run from spells
 *   and say a word now and then. Rain and snow thin the streets and hurry whoever is out.
 * - Angles are the original's (4096 a turn, 0 east, increasing toward the south).
 */
class UNREALMERIDIAN_API FMRAmbientLife
{
public:
	/** A room's doors (Shards' roomexits.json): door squares [row, col, the room it leads to], edges, arrivals */
	struct FExits
	{
		struct FDoor
		{
			int32 Row = 0, Col = 0;
			FString To;
		};
		struct FEdge
		{
			TCHAR Side = 'N';
			FString To;
		};
		TArray<FDoor> Doors;
		TArray<FEdge> Edges;
		TArray<FIntPoint> Arrivals;  // (row, col)
	};

	/** What the crowd needs to know each frame (ambientLife.ts AmbientFrame) */
	struct FFrame
	{
		bool bHasPlayer = false;
		FVector2D Player = FVector2D::ZeroVector;
		/** Which way the player looks (4096 a turn), or -1: people come and go out of sight */
		int32 Facing = -1;
		/** The Ambient Life setting: 0.5 few, 1 normal, 2 many */
		double Scale = 1.0;
		/** Real objects that block (shopkeepers, tables): walkers keep out of their way */
		TArray<FVector2D> Obstacles;
		/** The game hour, or -1 */
		double Hour = -1.0;
		MRAmbient::EWeather Weather = MRAmbient::EWeather::None;
		/** The player wears armour worth a second look */
		bool bImpressive = false;
	};

	struct FWalker
	{
		uint32 Id = 0;
		MRAmbient::FLook Look;
		MRAmbient::ERole Role = MRAmbient::ERole::Folk;
		MRAmbient::EPose Pose = MRAmbient::EPose::Stand;
		/** Counts up each time a pose starts (again): an emote that plays once is played again */
		int32 PoseSerial = 0;
		bool bWalking = false;
		TArray<FVector2D> Path;
		/** ROO units a millisecond */
		double Pace = 1.0;
		double IdleMs = 0.0;
		int32 FaceAngle = -1;
		bool bLeaving = false;
		/** 0..1 how visible, and which way it's going (1 in, -1 out, 0 neither) */
		double Fade = 1.0;
		int32 FadeDir = 0;
		double WaitedMs = 0.0, GreetCooldown = 0.0, SidestepMs = 0.0, GlanceMs = 0.0, StayMs = 0.0;
		uint32 FaceId = 0;
		bool bTorchBearer = false, bTorch = false;
		int32 Area = 0;
		bool bHasErrand = false;
		FVector2D Errand = FVector2D::ZeroVector;
		bool bFleeing = false;
		FString Say;
		double SayMs = 0.0;
		bool bHasBeat = false;
		FVector2D Beat[2];
		int32 BeatEnd = 0;
		double EmoteMs = 0.0, NextEmoteMs = 0.0;
		double WaveBackMs = 0.0;
		FString Name, FromRoom, ToRoom, ErrandRoom;
		FVector2D Pos = FVector2D::ZeroVector;
		int32 Angle = 0;
	};

	/** What the inspect window shows of someone (ambientLife.ts AmbientPersonInfo) */
	struct FPersonInfo
	{
		FString Name, Role, Doing, Mood, From, To, Errand;
		bool bFemale = false;
	};

	FMRAmbientLife(const MRAmbient::FRoomSpec& InSpec, const FExits& InExits, const FMRNavGrid& InGrid, uint32 Seed, uint32 IdBase);

	void Update(double DtMs, const FFrame& Frame);
	const TArray<FWalker>& GetWalkers() const { return Walkers; }
	/** Whether an id is someone here (not going) */
	bool Has(uint32 Id) const;
	bool Describe(uint32 Id, FPersonInfo& Out) const;
	/** A big spell went off at At: those within Radius run (guards, knights and fighters stand and look) */
	void Scatter(const FVector2D& At, double Radius);
	/** The player waved from P, facing Facing: the nearest in front (or right beside) turns and waves back */
	void WavedAt(const FVector2D& P, int32 Facing);
	/** Everyone goes at once */
	void Clear() { Walkers.Reset(); }
	const FMRNavGrid& GetGrid() const { return Grid; }
	const MRAmbient::FRoomSpec& GetSpec() const { return Spec; }

	static int32 AngleTo(double Dx, double Dy);

private:
	struct FExitPoint
	{
		FVector2D At;
		int32 Cell = -1;
		int32 Area = 0;
		bool bDoor = false;
		bool bErrand = false;
		FString To;
	};
	struct FPlan
	{
		TArray<FVector2D> Path;
		bool bHasErrand = false;
		FVector2D Errand = FVector2D::ZeroVector;
		bool bLeaving = false;
		FString To, ErrandRoom;
	};

	MRAmbient::FRoomSpec Spec;
	FExits Exits;
	FMRNavGrid Grid;
	MRAmbient::FRand Rand;
	MRAmbient::FRand NameRand;
	uint32 NextId;
	TArray<FExitPoint> Points;
	/** Towns: areas with somewhere to come from and go to, and how likely each is */
	TArray<TPair<int32, double>> Areas;
	TArray<int32> Cells;
	int32 MainArea = 1;
	double Busy = 1.0;
	TArray<FVector2D> Blockers;
	TArray<FWalker> Walkers;
	double SpawnMs = 0.0, SayMs = 2000.0, ObstacleMs = 0.0, PlayerAreaMs = 0.0, AgeMs = 0.0;
	int32 PlayerArea = 0;
	int32 GuardTries = 0;
	bool bPopulated = false;
	int32 ToPlace = 0;
	FFrame Frame;
	double Hour = -1.0;
	MRAmbient::EWeather Weather = MRAmbient::EWeather::None;

	void FindPoints();
	TArray<FVector2D> EdgePoints(TCHAR Side) const;
	void CloseAround(const TArray<FVector2D>& Obstacles);
	int32 PickArea();
	TArray<const FExitPoint*> PointsIn(int32 InArea, int32 Door = -1) const;
	int32 AreaOf(const FVector2D& P) const;
	bool PathToPoint(const FVector2D& From, const FExitPoint& P, TArray<FVector2D>& Out) const;
	bool Hidden(const FVector2D& P) const;
	int32 HiddenCell(int32 InArea);
	int32 OpenCellAwayFrom(int32 InArea, bool bNear = false);
	MRAmbient::ERole NewRole();
	MRAmbient::FLook Dress(MRAmbient::ERole Role);
	int32 HomeArea();
	bool Stays() const { return Spec.Kind != MRAmbient::EKind::Town; }
	FWalker& Spawn(const FVector2D& At, int32 Angle, bool bFadeIn, MRAmbient::ERole Role, int32 InArea);
	void Populate(int32 Target);
	bool PlanTown(const FVector2D& From, int32 InArea, FPlan& Out);
	void PlaceOnTheWay();
	void GatherPairs();
	int32 BesideCell(const FVector2D& O, int32 InArea);
	void KeepPopulation(int32 Target, double DtMs);
	void KeepGuards(int32 N);
	bool FindBeat(int32 InArea, FVector2D Out[2]);
	void Emote(FWalker& W, MRAmbient::EPose Pose, double Ms);
	void Fidget(FWalker& W);
	MRAmbient::FChatter Context(const FWalker& W) const;
	void SayLine(FWalker& W, const FString& Text);
	void Chatter(double DtMs);
	void Walk(FWalker& W, const TArray<FVector2D>& Path, bool bLeaving);
	void Leave(FWalker& W);
	bool Flee(FWalker& W, const FVector2D& From);
	void Repath(FWalker& W);
	void Stand(FWalker& W, double Ms);
	void Nudge(FWalker& W);
	void Pause(FWalker& W, double Ms);
	void SetPose(FWalker& W, MRAmbient::EPose Pose, bool bAgain = false);
	void TurnTowards(FWalker& W, int32 Angle, double DtMs);
	bool Sidestep(FWalker& W, const FVector2D& Player, const FVector2D& Next);
	void Shuffle(FWalker& W, const FVector2D& Player);
	void Step(FWalker& W, double DtMs);
	void Arrive(FWalker& W);
	void Wander(FWalker& W);
	FWalker* Find(uint32 Id);
	bool FindPath(const FVector2D& From, const FVector2D& To, TArray<FVector2D>& Out) const { return Grid.FindPath(From, To, Out); }
};

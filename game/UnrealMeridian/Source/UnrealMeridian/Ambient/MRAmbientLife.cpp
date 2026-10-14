#include "Ambient/MRAmbientLife.h"

using namespace MRAmbient;

namespace
{
	constexpr double SQ = 1024.0;
	/** A walker stopping steps aside this far, so sprites don't share a spot */
	constexpr double NudgeMin = 24.0, NudgeMax = 56.0;
	constexpr double FadeMs = 600.0;
	/** Walking pace, ROO units a millisecond (a player walks about 3 squares a second) */
	constexpr double PaceMin = 1.0, PaceMax = 1.6;
	/** Running from a spell, and hurrying through rain or snow */
	constexpr double FleePace = 2.4, WetPace = 1.3;
	/** Turning, angle units a millisecond */
	constexpr double TurnRate = 6.0;
	constexpr double WaitForPlayerMs = 1600.0;
	constexpr double GreetDistance = 1.6 * SQ;
	constexpr double WaveRange = 6.0 * SQ, WaveNear = 2.5 * SQ;
	constexpr double GreetCooldownMs = 30000.0;
	constexpr double SidestepDistance = 1.4 * SQ, SidestepCooldownMs = 2500.0;
	constexpr double GlanceDistance = 3.5 * SQ, GlanceCooldownMs = 45000.0;
	constexpr double SayBaseMs = 3200.0, SpeakRange = 10.0 * SQ;
	constexpr int32 MaxSpeaking = 2;
	constexpr double ErrandChance = 0.3;
	constexpr int32 MinAreaCells = 200;
	constexpr double NearPlayer = 18.0 * SQ;
	constexpr double BeatMin = 5.0 * SQ, BeatMax = 10.0 * SQ;
	constexpr double ObstacleRadius = 0.55 * SQ, ObstacleRefreshMs = 1500.0;
	constexpr double TorchBearers = 0.8;
	/** Moods for when nothing in particular is going on, one per person (by their name) */
	const TCHAR* Moods[] = {TEXT("Content"), TEXT("Cheerful"), TEXT("Thoughtful"), TEXT("Weary"), TEXT("Busy"), TEXT("Bored"), TEXT("Curious"),
		TEXT("Grumpy"), TEXT("Hopeful"), TEXT("Distracted")};

	double Dist(const FVector2D& A, const FVector2D& B) { return FVector2D::Distance(A, B); }

	uint32 NameHash(const FString& S)
	{
		uint32 H = 0x811c9dc5u;
		for (const TCHAR C : S)
		{
			H = (H ^ uint32(C)) * 0x01000193u;
		}
		return H;
	}

	/** Kod square (1-based row and column) -> its centre, ROO units */
	FVector2D KodSquare(double Row, double Col)
	{
		return FVector2D((Col - 1.0) * SQ + SQ / 2.0, (Row - 1.0) * SQ + SQ / 2.0);
	}

	int32 AngleDiff(int32 A, int32 B)
	{
		return ((A - B + 6144) % 4096) - 2048;
	}
}

int32 FMRAmbientLife::AngleTo(double Dx, double Dy)
{
	return FMath::RoundToInt(FMath::Atan2(Dy, Dx) * 4096.0 / (2.0 * UE_DOUBLE_PI)) & 4095;
}

FMRAmbientLife::FMRAmbientLife(const FRoomSpec& InSpec, const FExits& InExits, const FMRNavGrid& InGrid, uint32 Seed, uint32 IdBase)
	: Spec(InSpec), Exits(InExits), Grid(InGrid), Rand(Seed), NameRand(Seed ^ 0x5bd1e995u), NextId(IdBase)
{
	FMemory::Memzero(Grid.Closed.GetData(), Grid.Closed.Num());
	FindPoints();
}

bool FMRAmbientLife::Has(uint32 Id) const
{
	return Walkers.ContainsByPredicate([Id](const FWalker& W) { return W.Id == Id && W.FadeDir >= 0; });
}

FMRAmbientLife::FWalker* FMRAmbientLife::Find(uint32 Id)
{
	return Walkers.FindByPredicate([Id](const FWalker& W) { return W.Id == Id; });
}

bool FMRAmbientLife::Describe(uint32 Id, FPersonInfo& Out) const
{
	const FWalker* W = Walkers.FindByPredicate([Id](const FWalker& V) { return V.Id == Id; });
	if (!W)
	{
		return false;
	}
	const EKind Kind = Spec.Kind;
	Out.Name = W->Name;
	Out.bFemale = W->Look.bFemale;
	Out.Role = RoleLabel(W->Role);
	if (W->bFleeing) Out.Doing = TEXT("Running from danger");
	else if (W->bHasBeat) Out.Doing = W->Role == ERole::Knight ? TEXT("Standing watch") : TEXT("Keeping the peace on their beat");
	else if (W->bWalking && W->bHasErrand) Out.Doing = TEXT("Off on an errand");
	else if (W->bWalking && W->bLeaving) Out.Doing = W->ToRoom.IsEmpty() ? TEXT("Heading out") : TEXT("On the way somewhere");
	else if (W->bWalking) Out.Doing = TEXT("Strolling about");
	else if (W->bHasErrand) Out.Doing = TEXT("Trading at a shop's door");
	else if (W->EmoteMs > 0.0 && W->Pose == EPose::Dance) Out.Doing = TEXT("Dancing");
	else if (Kind == EKind::Inn) Out.Doing = TEXT("Drinking and talking");
	else if (Kind == EKind::Shop) Out.Doing = TEXT("Looking over the wares");
	else if (Kind == EKind::Castle) Out.Doing = TEXT("Attending the court");
	else Out.Doing = TEXT("Standing about");
	if (W->bFleeing) Out.Mood = TEXT("Frightened");
	else if (!W->Say.IsEmpty() && W->WaveBackMs > 0.0) Out.Mood = TEXT("Friendly");
	else if (Weather == EWeather::Rain && Kind == EKind::Town) Out.Mood = TEXT("Soggy");
	else if (Weather == EWeather::Snow && Kind == EKind::Town) Out.Mood = TEXT("Chilly");
	else if (Kind == EKind::Inn && Hour >= 0.0 && (Hour >= 20.0 || Hour < 2.0)) Out.Mood = TEXT("Merry");
	else Out.Mood = Moods[NameHash(W->Name) % UE_ARRAY_COUNT(Moods)];
	Out.From = W->FromRoom;
	Out.To = W->ToRoom;
	Out.Errand = W->ErrandRoom;
	return true;
}

void FMRAmbientLife::Scatter(const FVector2D& At, double Radius)
{
	bool bCried = false;
	for (FWalker& W : Walkers)
	{
		if (W.FadeDir < 0 || W.bFleeing || Dist(W.Pos, At) > Radius)
		{
			continue;
		}
		if (W.Role == ERole::Guard || W.Role == ERole::Knight || W.Role == ERole::Fighter)
		{
			W.FaceAngle = AngleTo(At.X - W.Pos.X, At.Y - W.Pos.Y);
			continue;
		}
		if (Flee(W, At) && !bCried)
		{
			bCried = true;
			if (Rand() < 0.7)
			{
				SayLine(W, FData::Get().ScatterLine(Rand));
			}
		}
	}
}

void FMRAmbientLife::WavedAt(const FVector2D& P, int32 Facing)
{
	FWalker* Best = nullptr;
	for (FWalker& W : Walkers)
	{
		if (W.FadeDir < 0 || W.Fade < 1.0 || W.bFleeing || W.WaveBackMs > 0.0)
		{
			continue;
		}
		const double D = Dist(W.Pos, P);
		const bool bAhead = FMath::Abs(AngleDiff(AngleTo(W.Pos.X - P.X, W.Pos.Y - P.Y), Facing)) < 700;
		if (D < WaveRange && (bAhead || D < WaveNear) && (!Best || D < Dist(Best->Pos, P)))
		{
			Best = &W;
		}
	}
	if (!Best)
	{
		return;
	}
	FWalker& W = *Best;
	if (W.bWalking)
	{
		Pause(W, 2500.0 + Rand() * 1500.0);
	}
	else
	{
		W.IdleMs = FMath::Max(W.IdleMs, 2000.0);
	}
	W.EmoteMs = 0.0;
	W.FaceAngle = AngleTo(P.X - W.Pos.X, P.Y - W.Pos.Y);
	W.GreetCooldown = GreetCooldownMs;
	W.WaveBackMs = 450.0 + Rand() * 400.0;
	const bool bArmed = !W.Look.Weapon.IsEmpty();
	if (W.Say.IsEmpty() && (bArmed || Rand() < 0.4))
	{
		FChatter C = Context(W);
		C.bGreeting = true;
		SayLine(W, FData::Get().ChatterLine(Rand, C));
	}
}

void FMRAmbientLife::Update(double DtMs, const FFrame& InFrame)
{
	Frame = InFrame;
	AgeMs += DtMs;
	if ((ObstacleMs -= DtMs) <= 0.0)
	{
		ObstacleMs = ObstacleRefreshMs;
		CloseAround(Frame.Obstacles);
	}
	if (Frame.bHasPlayer && (PlayerAreaMs -= DtMs) <= 0.0)
	{
		PlayerAreaMs = 1000.0;
		const int32 Cell = Grid.NearestCell(Frame.Player.X, Frame.Player.Y, 2);
		PlayerArea = Cell >= 0 ? Grid.Area[Cell] : 0;
	}
	const EKind Kind = Spec.Kind;
	Hour = Frame.Hour;
	Weather = Frame.Weather;
	Busy = Frame.Scale * FData::Get().BusyAt(Kind, Hour) * WeatherScale(Kind, Weather);
	const int32 Target = FMath::RoundToInt(Spec.Count * Busy);
	// guards are on duty whatever the hour; Few halves them, Many doesn't add any
	const int32 GuardCount = Frame.Scale > 0.0 ? FMath::RoundToInt(Spec.Guards * FMath::Min(1.0, Frame.Scale)) : 0;
	if (Target == 0 && GuardCount == 0)
	{
		Clear();
		return;
	}
	if (!bPopulated)
	{
		bPopulated = true;
		Populate(Target);
	}
	for (int32 K = 0; K < 2 && ToPlace > 0; ++K, --ToPlace)
	{
		PlaceOnTheWay();
	}
	KeepGuards(GuardCount);
	KeepPopulation(Target, DtMs);
	const bool bDark = Kind == EKind::Town && TorchHours(Hour);
	for (FWalker& W : Walkers)
	{
		W.bTorch = bDark && W.bTorchBearer;
		if (!W.Say.IsEmpty() && (W.SayMs -= DtMs) <= 0.0)
		{
			W.Say.Reset();
		}
	}
	Chatter(DtMs);
	for (int32 I = 0; I < Walkers.Num(); ++I)
	{
		Step(Walkers[I], DtMs);
	}
	Walkers.RemoveAll([](const FWalker& W) { return W.FadeDir < 0 && W.Fade <= 0.0; });
}

// ------------------------------------------------------------------ places

void FMRAmbientLife::FindPoints()
{
	Points.Reset();
	const auto Add = [this](const FVector2D& At, bool bDoor, bool bErrand, const FString& To)
	{
		// the nearest cell from which the doorway can be reached in a straight line: walkers stop there,
		// then step into the doorway itself
		const int32 Cell = Grid.NearestCell(At.X, At.Y, 3);
		if (Cell >= 0)
		{
			Points.Add(FExitPoint{At, Cell, Grid.Area[Cell], bDoor, bErrand, To.ToLower()});
		}
	};
	// neighbouring door squares make one door, entered at its middle
	struct FGroup
	{
		TArray<int32> Rows, Cols;
		FString To;
	};
	TArray<FGroup> Groups;
	for (const FExits::FDoor& D : Exits.Doors)
	{
		FGroup* G = Groups.FindByPredicate([&D](const FGroup& Grp)
		{
			if (Grp.To != D.To)
			{
				return false;
			}
			for (int32 K = 0; K < Grp.Rows.Num(); ++K)
			{
				if (FMath::Abs(Grp.Rows[K] - D.Row) <= 1 && FMath::Abs(Grp.Cols[K] - D.Col) <= 1)
				{
					return true;
				}
			}
			return false;
		});
		if (!G)
		{
			G = &Groups.AddDefaulted_GetRef();
			G->To = D.To;
		}
		G->Rows.Add(D.Row);
		G->Cols.Add(D.Col);
	}
	for (const FGroup& G : Groups)
	{
		double R = 0.0, C = 0.0;
		for (int32 K = 0; K < G.Rows.Num(); ++K)
		{
			R += G.Rows[K];
			C += G.Cols[K];
		}
		R /= G.Rows.Num();
		C /= G.Cols.Num();
		Add(KodSquare(R, C), true, FData::Get().IsErrandRoom(G.To), G.To);
	}
	// where players come out of a door is beside it: only for doors with no way in found above
	const int32 NumDoors = Points.Num();
	for (const FIntPoint& A : Exits.Arrivals)
	{
		const FVector2D At = KodSquare(A.X, A.Y);
		bool bNear = false;
		for (int32 K = 0; K < NumDoors; ++K)
		{
			bNear |= Dist(Points[K].At, At) < 2.5 * SQ;
		}
		if (!bNear)
		{
			Add(At, false, false, FString());
		}
	}
	for (const FExits::FEdge& E : Exits.Edges)
	{
		for (const FVector2D& At : EdgePoints(E.Side))
		{
			Add(At, true, false, E.To);
		}
	}
	Cells = Grid.AreaSizes();
	TMap<int32, int32> Counts;
	for (const FExitPoint& P : Points)
	{
		Counts.FindOrAdd(P.Area)++;
	}
	// towns: any area with a door, the main streets (more doors, more room) most of all
	Areas.Reset();
	for (const TPair<int32, int32>& C : Counts)
	{
		if (C.Key > 0)
		{
			Areas.Emplace(C.Key, (Cells.IsValidIndex(C.Key) ? Cells[C.Key] : 0) * (C.Value >= 2 ? 1.0 : 0.4));
		}
	}
	Areas.Sort([](const TPair<int32, double>& A, const TPair<int32, double>& B) { return A.Value > B.Value; });
	if (!Areas.Num())
	{
		Areas.Emplace(1, 1.0);  // no doors known: the largest open area
	}
	int32 Busiest = 0, BestCount = -1, BestCells = -1;
	for (const TPair<int32, int32>& C : Counts)
	{
		const int32 Size = Cells.IsValidIndex(C.Key) ? Cells[C.Key] : 0;
		if (C.Key > 0 && (C.Value > BestCount || (C.Value == BestCount && Size > BestCells)))
		{
			Busiest = C.Key;
			BestCount = C.Value;
			BestCells = Size;
		}
	}
	MainArea = Busiest > 0 ? Busiest : 1;
}

TArray<FVector2D> FMRAmbientLife::EdgePoints(TCHAR Side) const
{
	TArray<FVector2D> Out;
	const bool bNS = Side == 'N' || Side == 'S';
	const int32 Along = bNS ? Grid.Cols : Grid.Rows;
	for (int32 K = 0; K < Along; K += 3)
	{
		for (int32 Depth = 0; Depth < 4; ++Depth)
		{
			const int32 C = bNS ? K : Side == 'W' ? Depth : Grid.Cols - 1 - Depth;
			const int32 R = !bNS ? K : Side == 'N' ? Depth : Grid.Rows - 1 - Depth;
			const int32 Cell = R * Grid.Cols + C;
			if (Grid.Walk.IsValidIndex(Cell) && Grid.Walk[Cell] && Grid.Area[Cell] == 1)
			{
				Out.Add(Grid.CellCentre(Cell));
				break;
			}
		}
	}
	// a handful, spread out
	TArray<FVector2D> Few;
	const int32 Every = FMath::Max(1, Out.Num() / 4);
	for (int32 I = 0; I < Out.Num() && Few.Num() < 4; I += Every)
	{
		Few.Add(Out[I]);
	}
	return Few;
}

void FMRAmbientLife::CloseAround(const TArray<FVector2D>& Obstacles)
{
	// close the cells next to things that block, so paths go round them
	FMemory::Memzero(Grid.Closed.GetData(), Grid.Closed.Num());
	Blockers = Obstacles;
	const int32 R = FMath::CeilToInt(ObstacleRadius / FMRNavGrid::Cell);
	for (const FVector2D& O : Obstacles)
	{
		const int32 C0 = FMath::RoundToInt(O.X / FMRNavGrid::Cell), R0 = FMath::RoundToInt(O.Y / FMRNavGrid::Cell);
		for (int32 Rr = R0 - R; Rr <= R0 + R; ++Rr)
		{
			for (int32 Cc = C0 - R; Cc <= C0 + R; ++Cc)
			{
				if (Cc < 0 || Rr < 0 || Cc >= Grid.Cols || Rr >= Grid.Rows)
				{
					continue;
				}
				const int32 I = Rr * Grid.Cols + Cc;
				if (Dist(Grid.CellCentre(I), O) < ObstacleRadius)
				{
					Grid.Closed[I] = 1;
				}
			}
		}
	}
}

int32 FMRAmbientLife::PickArea()
{
	// towns: where the next walker goes about; the player's own part of town half the time
	const int32 Pa = PlayerArea;
	if (Pa > 0 && Rand() < 0.5 && (Areas.ContainsByPredicate([Pa](const TPair<int32, double>& A) { return A.Key == Pa; })
		|| (Cells.IsValidIndex(Pa) && Cells[Pa] >= MinAreaCells)))
	{
		return Pa;
	}
	double Total = 0.0;
	for (const TPair<int32, double>& A : Areas)
	{
		Total += A.Value;
	}
	double R = Rand() * Total;
	for (const TPair<int32, double>& A : Areas)
	{
		if ((R -= A.Value) < 0.0)
		{
			return A.Key;
		}
	}
	return Areas[0].Key;
}

TArray<const FMRAmbientLife::FExitPoint*> FMRAmbientLife::PointsIn(int32 InArea, int32 Door) const
{
	TArray<const FExitPoint*> Out;
	for (const FExitPoint& P : Points)
	{
		if (P.Area == InArea && Grid.IsOpen(P.Cell) && (Door < 0 || P.bDoor == (Door != 0)))
		{
			Out.Add(&P);
		}
	}
	return Out;
}

int32 FMRAmbientLife::AreaOf(const FVector2D& P) const
{
	const int32 Cell = Grid.NearestCell(P.X, P.Y, 2);
	return Cell >= 0 ? Grid.Area[Cell] : 0;
}

bool FMRAmbientLife::PathToPoint(const FVector2D& From, const FExitPoint& P, TArray<FVector2D>& Out) const
{
	// a path to an exit point that ends in the doorway itself, not just near it
	const FVector2D Before = Grid.CellCentre(P.Cell);
	if (!FindPath(From, Before, Out))
	{
		return false;
	}
	if (Dist(Before, P.At) > 1.0)
	{
		Out.Add(P.At);
	}
	return true;
}

bool FMRAmbientLife::Hidden(const FVector2D& P) const
{
	// the player can't see a spot: far off, or behind them
	if (!Frame.bHasPlayer)
	{
		return true;
	}
	const double D = Dist(P, Frame.Player);
	if (D > 14.0 * SQ)
	{
		return true;
	}
	if (D < 2.0 * SQ)
	{
		return false;
	}
	if (Frame.Facing < 0)
	{
		return D > 8.0 * SQ;
	}
	return FMath::Abs(AngleDiff(AngleTo(P.X - Frame.Player.X, P.Y - Frame.Player.Y), Frame.Facing)) > 900;
}

int32 FMRAmbientLife::HiddenCell(int32 InArea)
{
	for (int32 K = 0; K < 30; ++K)
	{
		const int32 Cell = Grid.RandomCell([this]() { return Rand(); }, InArea);
		if (Cell < 0)
		{
			return -1;
		}
		if (Hidden(Grid.CellCentre(Cell)))
		{
			return Cell;
		}
	}
	return OpenCellAwayFrom(InArea);
}

int32 FMRAmbientLife::OpenCellAwayFrom(int32 InArea, bool bNear)
{
	int32 Fallback = -1;
	for (int32 K = 0; K < 40; ++K)
	{
		const int32 Cell = Grid.RandomCell([this]() { return Rand(); }, InArea);
		if (Cell < 0)
		{
			return -1;
		}
		const double D = Frame.bHasPlayer ? Dist(Grid.CellCentre(Cell), Frame.Player) : TNumericLimits<double>::Max();
		if (D < 1.5 * SQ)
		{
			continue;
		}
		if (!bNear || !Frame.bHasPlayer || D < NearPlayer)
		{
			return Cell;
		}
		if (Fallback < 0)
		{
			Fallback = Cell;
		}
	}
	return Fallback;
}

// ------------------------------------------------------------------ people

ERole FMRAmbientLife::NewRole()
{
	const double R = Rand();
	switch (Spec.Kind)
	{
	case EKind::Inn: return ERole::Patron;
	// mostly the court: few knights
	case EKind::Castle: return R < 0.1 ? ERole::Knight : R < 0.45 ? ERole::Lady : ERole::Noble;
	case EKind::Shop: return ERole::Customer;
	// the arena's foyer: all sorts
	case EKind::Arena: return R < 0.4 ? ERole::Fighter : R < 0.55 ? ERole::Mage : ERole::Spectator;
	default: return ERole::Folk;
	}
}

FLook FMRAmbientLife::Dress(ERole Role)
{
	switch (Role)
	{
	case ERole::Guard: return GuardLook(Rand, Spec.Livery);
	case ERole::Knight: return KnightLook(Rand, Spec.Faction.IsEmpty() ? FString(TEXT("princess")) : Spec.Faction);
	case ERole::Lady: return LadyLook(Rand);
	case ERole::Noble: return NobleLook(Rand);
	case ERole::Fighter: return FighterLook(Rand);
	case ERole::Mage: return MageLook(Rand);
	default: return RandomLook(Rand);
	}
}

int32 FMRAmbientLife::HomeArea()
{
	if (Spec.Kind == EKind::Town)
	{
		return PickArea();
	}
	const int32 Pa = PlayerArea;
	if (Pa > 0 && Pa != MainArea && Cells.IsValidIndex(Pa) && Cells[Pa] >= MinAreaCells && Rand() < 0.4)
	{
		return Pa;
	}
	return MainArea;
}

FMRAmbientLife::FWalker& FMRAmbientLife::Spawn(const FVector2D& At, int32 Angle, bool bFadeIn, ERole Role, int32 InArea)
{
	FWalker& W = Walkers.AddDefaulted_GetRef();
	W.Id = NextId++;
	W.Look = Dress(Role);
	W.Role = Role;
	W.Pos = FVector2D(FMath::RoundToDouble(At.X), FMath::RoundToDouble(At.Y));
	W.Angle = Angle & 4095;
	const bool bSlow = Role == ERole::Guard || Role == ERole::Knight || Role == ERole::Lady;
	W.Pace = (PaceMin + Rand() * (PaceMax - PaceMin)) * (bSlow ? 0.75 : 1.0);
	W.Fade = bFadeIn ? 0.0 : 1.0;
	W.FadeDir = bFadeIn ? 1 : 0;
	W.StayMs = Spec.Kind == EKind::Shop ? 20000.0 + Rand() * 40000.0 : 60000.0 + Rand() * 180000.0;
	W.bTorchBearer = Role == ERole::Guard || (Role == ERole::Folk && Rand() < TorchBearers);
	W.Area = InArea;
	W.NextEmoteMs = 2000.0 + Rand() * 8000.0;
	W.Name = FData::Get().PersonName(NameRand, Role, W.Look.bFemale);
	return W;
}

void FMRAmbientLife::Populate(int32 Target)
{
	// the crowd already out when you arrive
	if (Stays())
	{
		for (int32 K = 0; K < Target; ++K)
		{
			const ERole Role = NewRole();
			const int32 InArea = HomeArea();
			const int32 Cell = OpenCellAwayFrom(InArea, true);
			if (Cell < 0)
			{
				break;
			}
			FWalker& W = Spawn(Grid.CellCentre(Cell), int32(Rand() * 4096.0), false, Role, InArea);
			W.IdleMs = Rand() * 15000.0;
		}
		GatherPairs();
		return;
	}
	// towns: most are already on their way somewhere
	ToPlace = FMath::CeilToInt(Target * 0.8);
}

bool FMRAmbientLife::PlanTown(const FVector2D& From, int32 InArea, FPlan& Out)
{
	// towns: sometimes a shop's door first (an errand), else another door to go in by, else (an area
	// without doors) somewhere out of sight to fade away
	TArray<const FExitPoint*> Errands = PointsIn(InArea).FilterByPredicate([&From](const FExitPoint* P) { return P->bErrand && Dist(P->At, From) >= 4.0 * SQ; });
	if (Errands.Num() && Rand() < ErrandChance * (Weather != EWeather::None ? 0.4 : 1.0))
	{
		const FExitPoint* P = Errands[Rand.Index(Errands.Num())];
		if (FindPath(From, Grid.CellCentre(P->Cell), Out.Path))
		{
			Out.bHasErrand = true;
			Out.Errand = P->At;
			Out.bLeaving = false;
			Out.ErrandRoom = P->To;
			return true;
		}
	}
	TArray<const FExitPoint*> Doors = PointsIn(InArea).FilterByPredicate([&From](const FExitPoint* P) { return Dist(P->At, From) >= 8.0 * SQ; });
	if (Doors.Num())
	{
		const FExitPoint* Door = Doors[Rand.Index(Doors.Num())];
		if (PathToPoint(From, *Door, Out.Path))
		{
			Out.bLeaving = true;
			Out.To = Door->To;
			return true;
		}
	}
	const int32 Cell = HiddenCell(InArea);
	if (Cell >= 0 && FindPath(From, Grid.CellCentre(Cell), Out.Path))
	{
		Out.bLeaving = true;
		return true;
	}
	return false;
}

void FMRAmbientLife::PlaceOnTheWay()
{
	// towns: someone part way along their walk from one door to another
	const int32 InArea = PickArea();
	TArray<const FExitPoint*> Starts = PointsIn(InArea);
	const FExitPoint* Start = Starts.Num() ? Starts[Rand.Index(Starts.Num())] : nullptr;
	const int32 Cell = Start ? Start->Cell : OpenCellAwayFrom(InArea);
	if (Cell < 0)
	{
		return;
	}
	const FVector2D From = Grid.CellCentre(Cell);
	FPlan Plan;
	if (!PlanTown(From, InArea, Plan) || !Plan.Path.Num())
	{
		return;
	}
	TArray<FVector2D> Pts;
	Pts.Add(From);
	Pts.Append(Plan.Path);
	const int32 I = FMath::Min(Pts.Num() - 2, int32(Rand() * (Pts.Num() - 1)));
	const double T = Rand();
	const FVector2D At = Pts[I] + (Pts[I + 1] - Pts[I]) * T;
	if (Frame.bHasPlayer && Dist(At, Frame.Player) < 1.5 * SQ)
	{
		return;
	}
	FWalker& W = Spawn(At, AngleTo(Pts[I + 1].X - At.X, Pts[I + 1].Y - At.Y), false, ERole::Folk, InArea);
	TArray<FVector2D> Rest(Pts.GetData() + I + 1, Pts.Num() - I - 1);
	Walk(W, Rest, Plan.bLeaving);
	W.bHasErrand = Plan.bHasErrand;
	W.Errand = Plan.Errand;
	W.FromRoom = Start ? Start->To : FString();
	W.ToRoom = Plan.To;
	W.ErrandRoom = Plan.ErrandRoom;
}

void FMRAmbientLife::GatherPairs()
{
	// inns and castles: a few stand together in twos, facing each other
	TArray<int32> Idle;
	for (int32 I = 0; I < Walkers.Num(); ++I)
	{
		if (!Walkers[I].bWalking && !Walkers[I].bHasBeat && Walkers[I].Role != ERole::Fighter)
		{
			Idle.Add(I);
		}
	}
	for (int32 K = 0; K + 1 < Idle.Num(); K += 2)
	{
		if (Rand() < 0.5)
		{
			continue;
		}
		FWalker& A = Walkers[Idle[K]];
		FWalker& B = Walkers[Idle[K + 1]];
		if (A.Area != B.Area)
		{
			continue;
		}
		const int32 Spot = BesideCell(A.Pos, A.Area);
		if (Spot < 0)
		{
			continue;
		}
		B.Pos = Grid.CellCentre(Spot);
		A.FaceAngle = AngleTo(B.Pos.X - A.Pos.X, B.Pos.Y - A.Pos.Y);
		B.FaceAngle = AngleTo(A.Pos.X - B.Pos.X, A.Pos.Y - B.Pos.Y);
	}
}

int32 FMRAmbientLife::BesideCell(const FVector2D& O, int32 InArea)
{
	// an open cell about a square away from someone, or -1
	for (int32 K = 0; K < 12; ++K)
	{
		const double A = Rand() * 2.0 * UE_DOUBLE_PI;
		const int32 Cell = Grid.NearestCell(O.X + FMath::Cos(A) * SQ, O.Y + FMath::Sin(A) * SQ, 0, InArea);
		if (Cell >= 0 && Dist(Grid.CellCentre(Cell), O) > SQ * 0.6)
		{
			return Cell;
		}
	}
	return -1;
}

void FMRAmbientLife::KeepPopulation(int32 Target, double DtMs)
{
	// keep about Target people about (guards apart): in by the doors, or go when there are too many
	const bool bStays = Stays();
	TArray<int32> Staying;
	for (int32 I = 0; I < Walkers.Num(); ++I)
	{
		const FWalker& W = Walkers[I];
		if (!W.bHasBeat && (bStays ? !W.bLeaving : W.FadeDir >= 0))
		{
			Staying.Add(I);
		}
	}
	if (Staying.Num() > Target)
	{
		int32 Extra = Staying.Num() - Target;
		for (const int32 I : Staying)
		{
			if (Extra > 0 && !Walkers[I].bLeaving)
			{
				Leave(Walkers[I]);
				--Extra;
			}
		}
		return;
	}
	if (Staying.Num() + ToPlace >= Target || (SpawnMs -= DtMs) > 0.0)
	{
		return;
	}
	// sooner the emptier it is
	const double Full = Target ? double(Staying.Num()) / Target : 1.0;
	SpawnMs = (bStays ? 4000.0 + Rand() * 10000.0 : 500.0 + Rand() * 2500.0) * (0.3 + Full);
	const ERole Role = NewRole();
	const int32 InArea = HomeArea();
	// in by a door (from the doorway itself), or out of sight where there's none
	TArray<const FExitPoint*> Starts = PointsIn(InArea).FilterByPredicate([this, bStays](const FExitPoint* P)
	{
		return !Frame.bHasPlayer || (Dist(P->At, Frame.Player) > 1.5 * SQ && (!bStays || Dist(P->At, Frame.Player) < NearPlayer));
	});
	FVector2D From;
	FString FromRoom;
	TArray<FVector2D> Lead;
	if (Starts.Num())
	{
		const FExitPoint* P = Starts[Rand.Index(Starts.Num())];
		From = P->At;
		FromRoom = P->To;
		Lead.Add(Grid.CellCentre(P->Cell));
	}
	else
	{
		const int32 Cell = HiddenCell(InArea);
		if (Cell < 0)
		{
			return;
		}
		From = Grid.CellCentre(Cell);
	}
	FWalker& W = Spawn(From, int32(Rand() * 4096.0), true, Role, InArea);
	W.FromRoom = FromRoom;
	const FVector2D Start = Lead.Num() ? Lead[0] : From;
	if (bStays)
	{
		// in from the door, and somewhere to stand
		const int32 Cell = OpenCellAwayFrom(InArea, true);
		TArray<FVector2D> Path;
		if (Cell >= 0 && FindPath(Start, Grid.CellCentre(Cell), Path))
		{
			TArray<FVector2D> All = Lead;
			All.Append(Path);
			Walk(W, All, false);
		}
		else
		{
			W.IdleMs = 5000.0;
		}
		return;
	}
	FPlan Plan;
	if (PlanTown(Start, InArea, Plan))
	{
		TArray<FVector2D> All = Lead;
		All.Append(Plan.Path);
		Walk(W, All, Plan.bLeaving);
		W.bHasErrand = Plan.bHasErrand;
		W.Errand = Plan.Errand;
		W.ToRoom = Plan.To;
		W.ErrandRoom = Plan.ErrandRoom;
	}
	else
	{
		Leave(W);
	}
}

void FMRAmbientLife::KeepGuards(int32 N)
{
	// guards (towns) and knights (castles) on their beats: one more a frame until there are enough
	int32 Count = 0, Last = -1;
	for (int32 I = 0; I < Walkers.Num(); ++I)
	{
		if (Walkers[I].bHasBeat && Walkers[I].FadeDir >= 0)
		{
			++Count;
			Last = I;
		}
	}
	if (Count > N)
	{
		Walkers[Last].bLeaving = true;
		Walkers[Last].FadeDir = -1;
		return;
	}
	if (Count == N || GuardTries > 5)
	{
		return;
	}
	const int32 InArea = Spec.Kind == EKind::Town ? PickArea() : MainArea;
	FVector2D Beat[2];
	if (!FindBeat(InArea, Beat))
	{
		++GuardTries;
		return;
	}
	const double T = Rand();
	const FVector2D At = Beat[0] + (Beat[1] - Beat[0]) * T;
	FWalker& W = Spawn(At, 0, AgeMs > 1000.0, Spec.Kind == EKind::Town ? ERole::Guard : ERole::Knight, InArea);
	W.bHasBeat = true;
	W.Beat[0] = Beat[0];
	W.Beat[1] = Beat[1];
	W.BeatEnd = Rand() < 0.5 ? 0 : 1;
	W.Angle = AngleTo(Beat[W.BeatEnd].X - At.X, Beat[W.BeatEnd].Y - At.Y);
	Walk(W, {Beat[W.BeatEnd]}, false);
}

bool FMRAmbientLife::FindBeat(int32 InArea, FVector2D Out[2])
{
	// a straight stretch of open ground to pace, clear of doorways and the other guards' beats
	const auto Clear = [this](const FVector2D& P)
	{
		for (const FExitPoint& D : Points)
		{
			if (Dist(D.At, P) <= 1.5 * SQ)
			{
				return false;
			}
		}
		for (const FWalker& V : Walkers)
		{
			if (V.bHasBeat && (Dist(V.Beat[0], P) <= 4.0 * SQ || Dist(V.Beat[1], P) <= 4.0 * SQ))
			{
				return false;
			}
		}
		return true;
	};
	for (int32 K = 0; K < 40; ++K)
	{
		const int32 Cell = Grid.RandomCell([this]() { return Rand(); }, InArea);
		if (Cell < 0)
		{
			return false;
		}
		const FVector2D A = Grid.CellCentre(Cell);
		if ((Frame.bHasPlayer && (Dist(A, Frame.Player) < 2.0 * SQ || (K < 30 && Dist(A, Frame.Player) > 2.0 * NearPlayer))) || !Clear(A))
		{
			continue;
		}
		const double Dir = FMath::FloorToDouble(Rand() * 8.0) * UE_DOUBLE_PI / 4.0;
		const double Len = BeatMin + Rand() * (BeatMax - BeatMin);
		const int32 EndCell = Grid.CellAt(A.X + FMath::Cos(Dir) * Len, A.Y + FMath::Sin(Dir) * Len);
		if (EndCell < 0 || !Grid.IsOpen(EndCell) || Grid.Area[EndCell] != InArea)
		{
			continue;
		}
		const FVector2D B = Grid.CellCentre(EndCell);
		if (Dist(A, B) >= BeatMin * 0.9 && Clear(B) && Grid.LineClear(A, B))
		{
			Out[0] = A;
			Out[1] = B;
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------ emotes and speech

void FMRAmbientLife::Emote(FWalker& W, EPose Pose, double Ms)
{
	SetPose(W, Pose, true);
	W.EmoteMs = Ms;
}

void FMRAmbientLife::Fidget(FWalker& W)
{
	// standing about, now and then: patrons wave, point at whoever they're with, or dance; courtiers wave and point
	W.NextEmoteMs = 6000.0 + Rand() * 14000.0;
	if (!W.Look.Weapon.IsEmpty() || W.bTorch)
	{
		return;
	}
	const double R = Rand();
	const bool bInn = Spec.Kind == EKind::Inn;
	if (bInn && R < 0.1)
	{
		Emote(W, EPose::Dance, 6000.0 + Rand() * 9000.0);
		return;
	}
	if (bInn && R > 0.75)
	{
		return;  // a roll of the dice or a drink (Shards plays a sound; ours has none yet)
	}
	const FWalker* Near = Walkers.FindByPredicate([&W](const FWalker& V) { return &V != &W && V.FadeDir >= 0 && Dist(V.Pos, W.Pos) < 2.5 * SQ; });
	if (Near && R < 0.3)
	{
		W.FaceAngle = AngleTo(Near->Pos.X - W.Pos.X, Near->Pos.Y - W.Pos.Y);
		Emote(W, EPose::Point, 0.0);
		return;
	}
	if (R < 0.4)
	{
		Emote(W, EPose::Wave, 0.0);
	}
}

FChatter FMRAmbientLife::Context(const FWalker& W) const
{
	FChatter C;
	C.Place = Spec.Place;
	C.Role = W.Role;
	C.Hour = Hour;
	C.Weather = Weather;
	return C;
}

void FMRAmbientLife::SayLine(FWalker& W, const FString& Text)
{
	W.Say = Text;
	W.SayMs = SayBaseMs + Text.Len() * 40.0;
}

void FMRAmbientLife::Chatter(double DtMs)
{
	// now and then, someone near the player says something
	if ((SayMs -= DtMs) > 0.0)
	{
		return;
	}
	SayMs = (Spec.Kind == EKind::Inn ? 2500.0 : 4000.0) + Rand() * 7000.0;
	if (!Frame.bHasPlayer || Walkers.FilterByPredicate([](const FWalker& W) { return !W.Say.IsEmpty(); }).Num() >= MaxSpeaking)
	{
		return;
	}
	TArray<int32> Near;
	for (int32 I = 0; I < Walkers.Num(); ++I)
	{
		const FWalker& W = Walkers[I];
		if (W.Say.IsEmpty() && W.FadeDir >= 0 && W.Fade >= 1.0 && !W.bFleeing && Dist(W.Pos, Frame.Player) < SpeakRange)
		{
			Near.Add(I);
		}
	}
	if (Near.Num())
	{
		FWalker& W = Walkers[Near[Rand.Index(Near.Num())]];
		SayLine(W, FData::Get().ChatterLine(Rand, Context(W)));
	}
}

// ------------------------------------------------------------------ moving about

void FMRAmbientLife::Walk(FWalker& W, const TArray<FVector2D>& Path, bool bLeaving)
{
	W.EmoteMs = 0.0;
	W.Path = Path;
	W.bWalking = true;
	W.bLeaving = bLeaving;
	W.WaitedMs = 0.0;
	W.FaceAngle = -1;
	W.FaceId = 0;
	SetPose(W, EPose::Walk);
}

void FMRAmbientLife::Leave(FWalker& W)
{
	// head for a door and go in; with none, out of sight and fade away
	if (W.bLeaving && W.bWalking)
	{
		return;
	}
	W.bHasErrand = false;
	const FVector2D Here = W.Pos;
	const int32 InArea = AreaOf(Here);
	TArray<const FExitPoint*> Ways = PointsIn(InArea, 1);
	if (!Ways.Num())
	{
		Ways = PointsIn(InArea);
	}
	if (Ways.Num())
	{
		Ways.Sort([&Here](const FExitPoint& A, const FExitPoint& B) { return Dist(A.At, Here) < Dist(B.At, Here); });
		const int32 N = FMath::Min(3, Ways.Num());
		TArray<FVector2D> Path;
		if (PathToPoint(Here, *Ways[Rand.Index(N)], Path))
		{
			Walk(W, Path, true);
			return;
		}
	}
	else if (!Hidden(Here))
	{
		const int32 Cell = HiddenCell(InArea);
		TArray<FVector2D> Path;
		if (Cell >= 0 && FindPath(Here, Grid.CellCentre(Cell), Path) && Path.Num())
		{
			Walk(W, Path, true);
			return;
		}
	}
	W.bLeaving = true;
	W.FadeDir = -1;
}

bool FMRAmbientLife::Flee(FWalker& W, const FVector2D& From)
{
	// run from a spell: several squares away from it, then on about their business
	const int32 InArea = AreaOf(W.Pos);
	const double Base = W.Pos == From ? Rand() * 2.0 * UE_DOUBLE_PI : FMath::Atan2(W.Pos.Y - From.Y, W.Pos.X - From.X);
	for (int32 K = 0; K < 6; ++K)
	{
		const double A = Base + (Rand() - 0.5) * 1.6;
		const double Len = (5.0 + Rand() * 4.0) * SQ;
		const int32 Cell = Grid.NearestCell(W.Pos.X + FMath::Cos(A) * Len, W.Pos.Y + FMath::Sin(A) * Len, 3, InArea);
		TArray<FVector2D> Path;
		if (Cell < 0 || !FindPath(W.Pos, Grid.CellCentre(Cell), Path) || !Path.Num())
		{
			continue;
		}
		W.bHasErrand = false;
		Walk(W, Path, false);
		W.bFleeing = true;
		return true;
	}
	return false;
}

void FMRAmbientLife::Repath(FWalker& W)
{
	TArray<FVector2D> Path;
	if (W.Path.Num() && FindPath(W.Pos, W.Path.Last(), Path))
	{
		W.Path = Path;
	}
	else
	{
		Stand(W, 2000.0);
	}
}

void FMRAmbientLife::Stand(FWalker& W, double Ms)
{
	W.bWalking = false;
	W.Path.Reset();
	W.IdleMs = Ms;
	SetPose(W, EPose::Stand);
	Nudge(W);
}

void FMRAmbientLife::Nudge(FWalker& W)
{
	// a small step aside on stopping, so two figures (or a figure and a prop) don't share one spot
	const double A = Rand() * 2.0 * UE_DOUBLE_PI, R = NudgeMin + Rand() * (NudgeMax - NudgeMin);
	const FVector2D P(FMath::RoundToDouble(W.Pos.X + FMath::Cos(A) * R), FMath::RoundToDouble(W.Pos.Y + FMath::Sin(A) * R));
	if (Grid.LineClear(W.Pos, P))
	{
		W.Pos = P;
	}
}

void FMRAmbientLife::Pause(FWalker& W, double Ms)
{
	// stop a while, then go on the way they were going
	const TArray<FVector2D> Rest = W.Path;
	const bool bLeaving = W.bLeaving;
	Stand(W, Ms);
	W.Path = Rest;
	W.bLeaving = bLeaving;
}

void FMRAmbientLife::SetPose(FWalker& W, EPose Pose, bool bAgain)
{
	if (W.Pose == Pose && !bAgain)
	{
		return;
	}
	W.Pose = Pose;
	++W.PoseSerial;
}

void FMRAmbientLife::TurnTowards(FWalker& W, int32 Angle, double DtMs)
{
	const int32 Diff = AngleDiff(Angle, W.Angle);
	const double Max = TurnRate * DtMs;
	W.Angle = (W.Angle + int32(FMath::Clamp(double(Diff), -Max, Max)) + 4096) & 4095;
}

bool FMRAmbientLife::Sidestep(FWalker& W, const FVector2D& Player, const FVector2D& Next)
{
	// the player right ahead: a step to the side away from them, then on
	const FVector2D O = W.Pos;
	const double Hl = FMath::Max(1.0, Dist(Next, O));
	const double Dx = (Next.X - O.X) / Hl, Dy = (Next.Y - O.Y) / Hl;
	const double Cross = Dx * (Player.Y - O.Y) - Dy * (Player.X - O.X);
	const int32 Side = Cross > 0.0 ? -1 : 1;
	W.SidestepMs = SidestepCooldownMs;
	for (const int32 S : {Side, -Side})
	{
		const FVector2D P(O.X - Dy * S * 0.9 * SQ + Dx * 0.4 * SQ, O.Y + Dx * S * 0.9 * SQ + Dy * 0.4 * SQ);
		const int32 Cell = Grid.CellAt(P.X, P.Y);
		if (Cell < 0 || !Grid.IsOpen(Cell) || !Grid.LineClear(O, P))
		{
			continue;
		}
		TArray<FVector2D> Rest;
		if (Grid.LineClear(P, Next))
		{
			Rest = W.Path;
		}
		else if (!W.Path.Num() || !FindPath(P, W.Path.Last(), Rest))
		{
			continue;
		}
		W.Path.Reset();
		W.Path.Add(P);
		W.Path.Append(Rest);
		return true;
	}
	return false;
}

void FMRAmbientLife::Shuffle(FWalker& W, const FVector2D& Player)
{
	// bumped into while standing: a step back out of the way, still facing them
	const FVector2D O = W.Pos;
	W.SidestepMs = SidestepCooldownMs;
	const double D = FMath::Max(1.0, Dist(O, Player));
	const FVector2D P = O + (O - Player) / D * 0.9 * SQ;
	const int32 Cell = Grid.CellAt(P.X, P.Y);
	if (Cell < 0 || !Grid.IsOpen(Cell) || !Grid.LineClear(O, P))
	{
		return;
	}
	TArray<FVector2D> Rest;
	if (W.Path.Num() && !FindPath(P, W.Path.Last(), Rest))
	{
		Rest = W.Path;
	}
	const bool bLeaving = W.bLeaving;
	const int32 Face = AngleTo(Player.X - O.X, Player.Y - O.Y);
	TArray<FVector2D> All;
	All.Add(P);
	All.Append(Rest);
	Walk(W, All, bLeaving);
	if (!Rest.Num())
	{
		W.FaceAngle = Face;
	}
}

void FMRAmbientLife::Step(FWalker& W, double DtMs)
{
	if (W.FadeDir)
	{
		W.Fade = FMath::Clamp(W.Fade + W.FadeDir * DtMs / FadeMs, 0.0, 1.0);
		if (W.Fade >= 1.0 && W.FadeDir > 0)
		{
			W.FadeDir = 0;
		}
	}
	if (W.FadeDir < 0)
	{
		return;
	}
	// waved at: the wave back, once they've turned to us
	if (W.WaveBackMs > 0.0 && (W.WaveBackMs -= DtMs) <= 0.0 && !W.bWalking && W.Look.Weapon.IsEmpty())
	{
		Emote(W, EPose::Wave, 0.0);
	}
	W.GreetCooldown -= DtMs;
	W.StayMs -= DtMs;
	W.SidestepMs -= DtMs;
	W.GlanceMs -= DtMs;
	const FVector2D Player = Frame.Player;
	const double Near = Frame.bHasPlayer ? Dist(W.Pos, Player) : TNumericLimits<double>::Max();
	const bool bTown = Spec.Kind == EKind::Town;

	// fine armour going by: a second look (and maybe a word)
	if (Frame.bImpressive && Frame.bHasPlayer && Near < GlanceDistance && W.GlanceMs <= 0.0 && !W.bFleeing && W.Role != ERole::Fighter)
	{
		W.GlanceMs = GlanceCooldownMs;
		if (Rand() < 0.6)
		{
			if (W.bWalking)
			{
				Pause(W, 1200.0 + Rand() * 1200.0);
			}
			W.FaceAngle = AngleTo(Player.X - W.Pos.X, Player.Y - W.Pos.Y);
			if (W.Say.IsEmpty() && Rand() < 0.45)
			{
				FChatter C = Context(W);
				C.bImpressed = true;
				SayLine(W, FData::Get().ChatterLine(Rand, C));
			}
		}
	}

	if (W.bWalking)
	{
		if (!W.Path.Num())
		{
			Arrive(W);
			return;
		}
		const FVector2D Next = W.Path[0];
		// the player in the way, just ahead: step aside, or wait for them to pass
		if (Frame.bHasPlayer && Near < SidestepDistance)
		{
			const double Hl = FMath::Max(1.0, Dist(Next, W.Pos));
			const double Ahead = ((Next.X - W.Pos.X) * (Player.X - W.Pos.X) + (Next.Y - W.Pos.Y) * (Player.Y - W.Pos.Y)) / (Hl * FMath::Max(1.0, Near));
			if (Ahead > 0.5)
			{
				const bool bAside = W.SidestepMs <= 0.0 && !W.bHasBeat && !W.bFleeing && Sidestep(W, Player, Next);
				if (!bAside && Near < 0.9 * SQ && (W.WaitedMs += DtMs) < WaitForPlayerMs)
				{
					SetPose(W, EPose::Stand);
					return;
				}
			}
		}
		else
		{
			W.WaitedMs = 0.0;
		}
		SetPose(W, EPose::Walk);
		const double Wet = Weather != EWeather::None && bTown && W.Role == ERole::Folk ? WetPace : 1.0;
		double Move = W.Pace * DtMs * (W.bFleeing ? FleePace : Wet);
		FVector2D P = W.Pos;
		while (Move > 0.0 && W.Path.Num())
		{
			const FVector2D T = W.Path[0];
			const double D = Dist(T, P);
			if (D <= Move)
			{
				P = T;
				Move -= D;
				W.Path.RemoveAt(0);
				// townsfolk stop now and then on the way to look about (less in the rain)
				if (bTown && W.Role == ERole::Folk && !W.bFleeing && W.Path.Num() > 1 && Rand() < (Weather != EWeather::None ? 0.02 : 0.06))
				{
					W.Pos = P;
					Pause(W, 1500.0 + Rand() * 4000.0);
					W.FaceAngle = int32(Rand() * 4096.0);
					return;
				}
			}
			else
			{
				P += (T - P) / D * Move;
				Move = 0.0;
			}
		}
		if (W.Path.Num())
		{
			TurnTowards(W, AngleTo(W.Path[0].X - P.X, W.Path[0].Y - P.Y), DtMs);
		}
		W.Pos = P;
		// an errand: stop at the shop's door a while, as if trading
		if (W.bHasErrand && Dist(W.Pos, W.Errand) < 1.3 * SQ)
		{
			Arrive(W);
		}
		return;
	}

	// standing about: an emote that lasts ends; another now and then
	if (W.EmoteMs > 0.0 && (W.EmoteMs -= DtMs) <= 0.0)
	{
		SetPose(W, EPose::Stand);
	}
	const bool bFidgets = Spec.Kind == EKind::Inn || W.Role == ERole::Lady || W.Role == ERole::Noble || W.Role == ERole::Spectator;
	if (bFidgets && W.EmoteMs <= 0.0 && (W.NextEmoteMs -= DtMs) <= 0.0)
	{
		Fidget(W);
	}
	if (Frame.bHasPlayer && Near < 0.75 * SQ && W.SidestepMs <= 0.0)
	{
		Shuffle(W, Player);
		return;
	}
	if (Frame.bHasPlayer && Near < GreetDistance && W.GreetCooldown <= 0.0)
	{
		// someone comes close: turn to them, and sometimes wave or say hello
		W.GreetCooldown = GreetCooldownMs;
		W.FaceAngle = AngleTo(Player.X - W.Pos.X, Player.Y - W.Pos.Y);
		if (Rand() < 0.4 && W.Look.Weapon.IsEmpty())
		{
			SetPose(W, EPose::Wave, true);
		}
		if (W.Say.IsEmpty() && Rand() < 0.35)
		{
			FChatter C = Context(W);
			C.bGreeting = true;
			SayLine(W, FData::Get().ChatterLine(Rand, C));
		}
	}
	if (W.FaceAngle >= 0)
	{
		TurnTowards(W, W.FaceAngle, DtMs);
	}
	if ((W.IdleMs -= DtMs) > 0.0)
	{
		return;
	}
	// paused on the way: go on
	if (W.Path.Num())
	{
		const TArray<FVector2D> Path = W.Path;
		Walk(W, Path, W.bLeaving);
		return;
	}
	if (W.bHasBeat)
	{
		// the other end of the beat
		W.BeatEnd ^= 1;
		const FVector2D End = W.Beat[W.BeatEnd];
		TArray<FVector2D> Path;
		if (Grid.LineClear(W.Pos, End) || !FindPath(W.Pos, End, Path))
		{
			Path = {End};
		}
		Walk(W, Path, false);
		return;
	}
	if (bTown)
	{
		Leave(W);
		return;
	}
	if (W.StayMs < 0.0 && Rand() < 0.5 && (PointsIn(W.Area).Num() || Hidden(W.Pos)))
	{
		Leave(W);
		return;
	}
	Wander(W);
}

void FMRAmbientLife::Arrive(FWalker& W)
{
	// the end of a walk: go in, stand a while, or (towns) head for a door
	if (W.bLeaving)
	{
		W.FadeDir = -1;
		SetPose(W, EPose::Stand);
		return;
	}
	const bool bFled = W.bFleeing;
	W.bFleeing = false;
	if (W.bHasBeat)
	{
		// at the end of the beat: a look along the way they came
		const FVector2D Other = W.Beat[W.BeatEnd ^ 1];
		Stand(W, 2000.0 + Rand() * 3000.0);
		W.FaceAngle = AngleTo(W.Pos.X - Other.X, W.Pos.Y - Other.Y);
		return;
	}
	if (W.bHasErrand)
	{
		const FVector2D Door = W.Errand;
		W.bHasErrand = false;
		Stand(W, 3000.0 + Rand() * 6000.0);
		W.FaceAngle = AngleTo(Door.X - W.Pos.X, Door.Y - W.Pos.Y);
		return;
	}
	if (Spec.Kind == EKind::Town)
	{
		Stand(W, bFled ? 1500.0 + Rand() * 2000.0 : 300.0);  // a breath (longer after running), then a door
		return;
	}
	// inns and the rest: stand a while, maybe facing whoever we came to talk to
	Stand(W, (bFled ? 2000.0 : 6000.0) + Rand() * 25000.0);
	if (const FWalker* Friend = W.FaceId ? Walkers.FindByPredicate([&W](const FWalker& V) { return V.Id == W.FaceId; }) : nullptr)
	{
		W.FaceAngle = AngleTo(Friend->Pos.X - W.Pos.X, Friend->Pos.Y - W.Pos.Y);
	}
	else if (Spec.Kind == EKind::Shop)
	{
		// whoever's at the counter faces the shopkeeper
		const FVector2D* By = nullptr;
		for (const FVector2D& B : Blockers)
		{
			if (Dist(B, W.Pos) < 1.8 * SQ && (!By || Dist(B, W.Pos) < Dist(*By, W.Pos)))
			{
				By = &B;
			}
		}
		if (By)
		{
			W.FaceAngle = AngleTo(By->X - W.Pos.X, By->Y - W.Pos.Y);
		}
	}
}

void FMRAmbientLife::Wander(FWalker& W)
{
	// inns and the rest: go and stand somewhere else in their part of the room, often next to someone
	const FVector2D Here = W.Pos;
	TArray<const FWalker*> Others;
	for (const FWalker& V : Walkers)
	{
		if (&V != &W && !V.bWalking && !V.bLeaving && !V.bHasBeat && V.Area == W.Area)
		{
			Others.Add(&V);
		}
	}
	int32 Cell = -1;
	uint32 FriendId = 0;
	// shops: up to the counter, beside the shopkeeper (or whatever else stands in the room), as often as not
	TArray<FVector2D> Counter;
	if (Spec.Kind == EKind::Shop && Rand() < 0.6)
	{
		Counter = Blockers.FilterByPredicate([&Here](const FVector2D& B) { return Dist(B, Here) < 10.0 * SQ; });
	}
	if (Counter.Num())
	{
		Cell = BesideCell(Counter[Rand.Index(Counter.Num())], W.Area);
	}
	else if (Others.Num() && Rand() < 0.4)
	{
		const FWalker* Friend = Others[Rand.Index(Others.Num())];
		FriendId = Friend->Id;
		Cell = BesideCell(Friend->Pos, W.Area);
	}
	for (int32 K = 0; Cell < 0 && K < 10; ++K)
	{
		const int32 C = Grid.RandomCell([this]() { return Rand(); }, W.Area);
		if (C >= 0 && Dist(Grid.CellCentre(C), Here) < 7.0 * SQ)
		{
			Cell = C;
		}
	}
	TArray<FVector2D> Path;
	if (Cell < 0 || !FindPath(Here, Grid.CellCentre(Cell), Path) || !Path.Num())
	{
		Stand(W, 5000.0 + Rand() * 10000.0);
		return;
	}
	Walk(W, Path, false);
	W.FaceId = FriendId;
}

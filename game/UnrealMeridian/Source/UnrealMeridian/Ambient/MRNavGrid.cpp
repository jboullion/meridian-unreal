#include "Ambient/MRNavGrid.h"

#include "Algo/Reverse.h"

namespace
{
	/** navGrid.ts DIRS: neighbour offsets by link bit, E, SE, S, SW, W, NW, N, NE */
	constexpr int32 Dirs[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

	int32 DirOf(int32 Dc, int32 Dr)
	{
		for (int32 D = 0; D < 8; ++D)
		{
			if (Dirs[D][0] == Dc && Dirs[D][1] == Dr)
			{
				return D;
			}
		}
		return -1;
	}
}

bool FMRNavGrid::Decode(const TArray<uint8>& Bytes)
{
	// navGrid.ts encodeNavGrid: "NAVG", format, the room's security, cols, rows (little endian), then
	// one walk bit a cell and one byte of links a cell
	if (Bytes.Num() < 16 || Bytes[0] != 'N' || Bytes[1] != 'A' || Bytes[2] != 'V' || Bytes[3] != 'G')
	{
		return false;
	}
	const auto U32 = [&Bytes](int32 At) { return uint32(Bytes[At]) | uint32(Bytes[At + 1]) << 8 | uint32(Bytes[At + 2]) << 16 | uint32(Bytes[At + 3]) << 24; };
	const auto U16 = [&Bytes](int32 At) { return int32(Bytes[At]) | int32(Bytes[At + 1]) << 8; };
	if (U32(4) != Format)
	{
		return false;
	}
	Security = U32(8);
	Cols = U16(12);
	Rows = U16(14);
	const int32 N = Cols * Rows;
	const int32 Bits = (N + 7) / 8;
	if (N <= 0 || Bytes.Num() != 16 + Bits + N)
	{
		Cols = Rows = 0;
		return false;
	}
	Walk.SetNumZeroed(N);
	Links.SetNumZeroed(N);
	Closed.SetNumZeroed(N);
	for (int32 I = 0; I < N; ++I)
	{
		Walk[I] = (Bytes[16 + (I >> 3)] >> (I & 7)) & 1;
		Links[I] = Bytes[16 + Bits + I];
	}
	LabelAreas();
	return true;
}

void FMRNavGrid::LabelAreas()
{
	// navGrid.ts labelAreas: connected areas over two-way links, numbered largest first
	const int32 N = Num();
	TArray<int32> Label;
	Label.Init(-1, N);
	TArray<int32> Sizes, Stack;
	for (int32 S = 0; S < N; ++S)
	{
		if (!Walk[S] || Label[S] >= 0)
		{
			continue;
		}
		const int32 Id = Sizes.Num();
		int32 Size = 0;
		Label[S] = Id;
		Stack.Add(S);
		while (Stack.Num())
		{
			const int32 I = Stack.Pop(EAllowShrinking::No);
			++Size;
			for (int32 D = 0; D < 8; ++D)
			{
				if (!(Links[I] & (1 << D)))
				{
					continue;
				}
				const int32 J = I + Dirs[D][1] * Cols + Dirs[D][0];
				if (J >= 0 && J < N && Label[J] < 0 && (Links[J] & (1 << ((D + 4) & 7))))
				{
					Label[J] = Id;
					Stack.Add(J);
				}
			}
		}
		Sizes.Add(Size);
	}
	TArray<int32> Order;
	for (int32 Id = 0; Id < Sizes.Num(); ++Id)
	{
		Order.Add(Id);
	}
	Order.StableSort([&Sizes](int32 A, int32 B) { return Sizes[A] > Sizes[B]; });
	TArray<int32> Rank;
	Rank.SetNumZeroed(Sizes.Num());
	for (int32 K = 0; K < Order.Num(); ++K)
	{
		Rank[Order[K]] = K + 1;
	}
	Area.SetNumZeroed(N);
	for (int32 I = 0; I < N; ++I)
	{
		Area[I] = Label[I] >= 0 ? uint16(FMath::Min(65535, Rank[Label[I]])) : 0;
	}
}

TArray<int32> FMRNavGrid::AreaSizes() const
{
	TArray<int32> Out;
	for (const uint16 A : Area)
	{
		if (A)
		{
			if (Out.Num() <= A)
			{
				Out.SetNumZeroed(A + 1);
			}
			++Out[A];
		}
	}
	return Out;
}

int32 FMRNavGrid::CellAt(double X, double Y) const
{
	const int32 C = FMath::FloorToInt(X / Cell + 0.5);
	const int32 R = FMath::FloorToInt(Y / Cell + 0.5);
	return C < 0 || R < 0 || C >= Cols || R >= Rows ? -1 : R * Cols + C;
}

bool FMRNavGrid::Linked(int32 From, int32 To, bool bBoth) const
{
	if (From < 0 || To < 0 || From >= Num() || To >= Num())
	{
		return false;
	}
	const int32 D = DirOf(To % Cols - From % Cols, To / Cols - From / Cols);
	if (D < 0 || !(Links[From] & (1 << D)))
	{
		return false;
	}
	return !bBoth || (Links[To] & (1 << ((D + 4) & 7)));
}

bool FMRNavGrid::LineClear(const FVector2D& A, const FVector2D& B, bool bOpenStart) const
{
	int32 Prev = CellAt(A.X, A.Y);
	if (Prev < 0 || (!bOpenStart && !IsOpen(Prev)))
	{
		return false;
	}
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / (Cell / 3.0)));
	for (int32 K = 1; K <= Steps; ++K)
	{
		const FVector2D P = A + (B - A) * (double(K) / Steps);
		const int32 I = CellAt(P.X, P.Y);
		if (I == Prev)
		{
			continue;
		}
		if (!IsOpen(I))
		{
			return false;
		}
		// a start off the walkable cells (a doorway) has no links of its own: the next cell will do
		if (Walk[Prev] && !Linked(Prev, I))
		{
			return false;
		}
		Prev = I;
	}
	return true;
}

int32 FMRNavGrid::NearestCell(double X, double Y, int32 MaxCells, int32 InArea, bool bReachable) const
{
	const int32 C0 = FMath::FloorToInt(X / Cell + 0.5), R0 = FMath::FloorToInt(Y / Cell + 0.5);
	TArray<TPair<double, int32>> Found;
	for (int32 R = R0 - MaxCells; R <= R0 + MaxCells; ++R)
	{
		for (int32 C = C0 - MaxCells; C <= C0 + MaxCells; ++C)
		{
			if (C < 0 || R < 0 || C >= Cols || R >= Rows)
			{
				continue;
			}
			const int32 I = R * Cols + C;
			if (!IsOpen(I) || (InArea && Area[I] != InArea))
			{
				continue;
			}
			Found.Emplace(FVector2D::Distance(CellCentre(I), FVector2D(X, Y)), I);
		}
	}
	Found.StableSort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	if (!bReachable)
	{
		return Found.Num() ? Found[0].Value : -1;
	}
	for (const TPair<double, int32>& F : Found)
	{
		if (F.Key < 1.0 || LineClear(FVector2D(X, Y), CellCentre(F.Value)))
		{
			return F.Value;
		}
	}
	return -1;
}

int32 FMRNavGrid::RandomCell(TFunctionRef<double()> Rand, int32 InArea, int32 Tries) const
{
	const int32 N = Num();
	for (int32 K = 0; K < Tries; ++K)
	{
		const int32 I = FMath::Min(N - 1, int32(Rand() * N));
		if (IsOpen(I) && (!InArea || Area[I] == InArea))
		{
			return I;
		}
	}
	return -1;
}

TArray<int32> FMRNavGrid::FindCells(int32 From, int32 To, int32 MaxVisits) const
{
	// navGrid.ts findCells: A* with the octile distance
	TArray<int32> Out;
	if (!IsOpen(From) || !IsOpen(To))
	{
		return Out;
	}
	if (From == To)
	{
		Out.Add(From);
		return Out;
	}
	const int32 N = Num();
	TArray<float> Cost;
	Cost.Init(TNumericLimits<float>::Max(), N);
	TArray<int32> Prev;
	Prev.Init(-1, N);
	TArray<uint8> Done;
	Done.SetNumZeroed(N);
	const int32 Tc = To % Cols, Tr = To / Cols;
	const auto H = [this, Tc, Tr](int32 I)
	{
		const int32 Dx = FMath::Abs(I % Cols - Tc), Dy = FMath::Abs(I / Cols - Tr);
		return float(FMath::Max(Dx, Dy) + (UE_SQRT_2 - 1.0) * FMath::Min(Dx, Dy));
	};
	struct FNode
	{
		float F;
		int32 I;
	};
	TArray<FNode> Heap;
	const auto Less = [](const FNode& A, const FNode& B) { return A.F < B.F; };
	Cost[From] = 0.f;
	Heap.HeapPush(FNode{H(From), From}, Less);
	int32 Visits = 0;
	while (Heap.Num())
	{
		FNode Top;
		Heap.HeapPop(Top, Less, EAllowShrinking::No);
		const int32 I = Top.I;
		if (Done[I])
		{
			continue;
		}
		if (I == To)
		{
			break;
		}
		Done[I] = 1;
		if (++Visits > MaxVisits)
		{
			return Out;
		}
		for (int32 D = 0; D < 8; ++D)
		{
			if (!(Links[I] & (1 << D)))
			{
				continue;
			}
			const int32 J = I + Dirs[D][1] * Cols + Dirs[D][0];
			if (J < 0 || J >= N || Done[J] || Closed[J])
			{
				continue;
			}
			const float C = Cost[I] + ((D & 1) ? UE_SQRT_2 : 1.f);
			if (C < Cost[J])
			{
				Cost[J] = C;
				Prev[J] = I;
				Heap.HeapPush(FNode{C + H(J), J}, Less);
			}
		}
	}
	if (Prev[To] < 0)
	{
		return Out;
	}
	Out.Add(To);
	for (int32 I = Prev[To]; I >= 0; I = Prev[I])
	{
		Out.Add(I);
	}
	Algo::Reverse(Out);
	return Out;
}

bool FMRNavGrid::FindPath(const FVector2D& From, const FVector2D& To, TArray<FVector2D>& Out) const
{
	// navGrid.ts findPath: the grid's path, pulled tight
	Out.Reset();
	const int32 A = NearestCell(From.X, From.Y);
	const int32 InArea = A >= 0 ? Area[A] : 0;
	const int32 B = NearestCell(To.X, To.Y, 4, InArea);
	const TArray<int32> Cells = FindCells(A, B);
	if (!Cells.Num())
	{
		return false;
	}
	TArray<FVector2D> Pts;
	Pts.Add(From);
	for (int32 K = 1; K < Cells.Num() - 1; ++K)
	{
		Pts.Add(CellCentre(Cells[K]));
	}
	Pts.Add(Cells.Num() > 1 ? CellCentre(B) : From);
	if (FVector2D::Distance(To, Pts.Last()) > 1.0 && LineClear(Pts.Last(), To))
	{
		Pts.Add(To);
	}
	int32 I = 0;
	while (I < Pts.Num() - 1)
	{
		// a few dozen points ahead at most: a long path costs no more than that
		int32 J = FMath::Min(Pts.Num() - 1, I + 40);
		while (J > I + 1 && !LineClear(Pts[I], Pts[J]))
		{
			--J;
		}
		Out.Add(Pts[J]);
		I = J;
	}
	return true;
}

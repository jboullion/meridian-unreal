#include "World/MRRoomMesh.h"

#include "Core/MRUnits.h"
#include "World/MRRooFile.h"

namespace
{
	constexpr double RooPerSquare = MRRoo::RooPerSquare;
	constexpr double Fine = MRRoo::RooPerFine;
	constexpr double MPerRoo = MRUnits::MetersPerSquare / RooPerSquare;
	// how far you sink into a sector with a wading depth: 0, 1/5, 2/5 and 3/5 of a square (roo2gltf DEPTH_SINK_ROO)
	constexpr double DepthSink[4] = {0.0, RooPerSquare / 5, 2 * RooPerSquare / 5, 3 * RooPerSquare / 5};

	/** A point in ROO space with its height: (x east, y south, z up), ROO units. */
	using FRoo = FVector;

	/** roo2gltf's glTF space (x, height, y): normals and winding are decided there, as in the Python. */
	FVector ToGltf(const FRoo& P) { return FVector(P.X, P.Z, P.Y) * MPerRoo; }

	bool NewellNormal(const TArray<FVector>& Pts, FVector& Out)
	{
		double NX = 0.0, NY = 0.0, NZ = 0.0;
		for (int32 i = 0; i < Pts.Num(); ++i)
		{
			const FVector& A = Pts[i];
			const FVector& B = Pts[(i + 1) % Pts.Num()];
			NX += (A.Y - B.Y) * (A.Z + B.Z);
			NY += (A.Z - B.Z) * (A.X + B.X);
			NZ += (A.X - B.X) * (A.Y + B.Y);
		}
		const double L = FMath::Sqrt(NX * NX + NY * NY + NZ * NZ);
		if (L < 1e-9)
		{
			return false;
		}
		Out = FVector(NX, NY, NZ) / L;
		return true;
	}

	struct FHeights
	{
		const FMRRooFile& Room;
		bool bWading;

		double Floor(int32 SectorNum, double X, double Y) const
		{
			const FMRRooSector& S = Room.Sectors[SectorNum - 1];
			const double Z = S.bSlopedFloor && S.FloorSlope.IsValid() ? S.FloorSlope.HeightAt(X, Y) : S.FloorHeight * Fine;
			return bWading ? Z - DepthSink[S.Depth()] : Z;
		}
		double Ceil(int32 SectorNum, double X, double Y) const
		{
			const FMRRooSector& S = Room.Sectors[SectorNum - 1];
			return S.bSlopedCeiling && S.CeilingSlope.IsValid() ? S.CeilingSlope.HeightAt(X, Y) : S.CeilingHeight * Fine;
		}
	};

	/** The sector (1-based) under a ROO point: the first leaf, in file order, whose polygon holds it. */
	class FSectorLookup
	{
	public:
		explicit FSectorLookup(const FMRRooFile& Room)
		{
			for (int32 i = 0; i < Room.Nodes.Num(); ++i)
			{
				const FMRRooNode& N = Room.Nodes[i];
				if (N.Type == FMRRooNode::Leaf && N.Sector && N.Points.Num() >= 3)
				{
					FBox2D Box(ForceInit);
					for (const FVector2f& P : N.Points)
					{
						Box += FVector2D(P);
					}
					Leaves.Add({&N.Points, N.Sector, Box});
				}
			}
			// buckets of leaf indices (in order) over a grid of CellRoo squares: roo2gltf scans every leaf
			for (int32 i = 0; i < Leaves.Num(); ++i)
			{
				const FBox2D& B = Leaves[i].Box;
				for (int32 CX = Cell(B.Min.X); CX <= Cell(B.Max.X); ++CX)
				{
					for (int32 CY = Cell(B.Min.Y); CY <= Cell(B.Max.Y); ++CY)
					{
						Grid.FindOrAdd(FIntPoint(CX, CY)).Add(i);
					}
				}
			}
		}

		int32 At(double X, double Y) const
		{
			const TArray<int32>* Cands = Grid.Find(FIntPoint(Cell(X), Cell(Y)));
			if (!Cands)
			{
				return 0;
			}
			for (const int32 i : *Cands)
			{
				if (Inside(*Leaves[i].Points, X, Y))
				{
					return Leaves[i].Sector;
				}
			}
			return 0;
		}

	private:
		static constexpr double CellRoo = 2048.0;
		static int32 Cell(double V) { return FMath::FloorToInt32(V / CellRoo); }

		static bool Inside(const TArray<FVector2f>& Pts, double X, double Y)
		{
			int32 Sign = 0;
			for (int32 i = 0; i < Pts.Num(); ++i)
			{
				const FVector2D A(Pts[i]);
				const FVector2D B(Pts[(i + 1) % Pts.Num()]);
				const double C = (B.X - A.X) * (Y - A.Y) - (B.Y - A.Y) * (X - A.X);
				if (FMath::Abs(C) < 1e-6)
				{
					continue;
				}
				if (Sign == 0)
				{
					Sign = C > 0 ? 1 : -1;
				}
				else if ((C > 0) != (Sign > 0))
				{
					return false;
				}
			}
			return true;
		}

		struct FLeaf
		{
			const TArray<FVector2f>* Points;
			int32 Sector;
			FBox2D Box;
		};
		TArray<FLeaf> Leaves;
		TMap<FIntPoint, TArray<int32>> Grid;
	};

	class FBuilder
	{
	public:
		FMRRoomMesh Mesh;

		/** A convex polygon (ROO points), fanned into triangles; light: a level or one per glTF normal. */
		void Poly(uint16 Tex, const TArray<FRoo>& Pts, const TArray<FVector2D>& UVs, TFunctionRef<double(const FVector&)> Light)
		{
			if (Pts.Num() < 3)
			{
				return;
			}
			TArray<FVector> G;
			for (const FRoo& P : Pts)
			{
				G.Add(ToGltf(P));
			}
			FVector N;
			if (!NewellNormal(G, N))
			{
				return;
			}
			const float Level = static_cast<float>(Light(N));
			FMRRoomMeshSection& S = Section(Tex);
			const int32 Base = S.Positions.Num();
			const FVector UENormal(N.X, N.Z, N.Y);
			for (int32 i = 0; i < Pts.Num(); ++i)
			{
				S.Positions.Add(Pts[i] * MRUnits::CmPerRoo);
				S.Normals.Add(UENormal);
				S.UVs.Add(UVs[i]);
				S.Light.Add(Level);
			}
			// (x, height, y) -> (x, y, height) mirrors, so the winding flips too: faces keep facing
			// the way their normal points (floors up), as UE's glTF import of roo2gltf's meshes does
			for (int32 i = 1; i + 1 < Pts.Num(); ++i)
			{
				S.Triangles.Append({Base, Base + i + 1, Base + i});
			}
		}

	private:
		FMRRoomMeshSection& Section(uint16 Tex)
		{
			if (const int32* Index = SectionOf.Find(Tex))
			{
				return Mesh.Sections[*Index];
			}
			SectionOf.Add(Tex, Mesh.Sections.Num());
			FMRRoomMeshSection& S = Mesh.Sections.AddDefaulted_GetRef();
			S.Texture = Tex;
			return S;
		}
		TMap<uint16, int32> SectionOf;
	};

	enum class ESection : uint8 { Normal, Below, Above };

	/** roo2gltf wall_uvs: heights (maybe clipped for WF_NO_VTILE) and the corners' UVs. */
	void WallUVs(const FVector2D& Repeat, const FMRRooSidedef* Sd, ESection Section, int32 Side, const FMRRooWall& W, double Length,
		double& B0, double& T0, double& B1, double& T1, FVector2D& UvB0, FVector2D& UvT0, FVector2D& UvB1, FVector2D& UvT1)
	{
		const double RW = Repeat.X, RH = Repeat.Y;
		const uint32 Flags = Sd ? Sd->Flags : 0;
		const double XOff = (Side > 0 ? W.PosXOffset : W.NegXOffset) * Fine;
		const double YOff = (Side > 0 ? W.PosYOffset : W.NegYOffset) * Fine;
		double Start = XOff / RW, End = (XOff + Length) / RW;
		if (Flags & MRRoo::WF_BACKWARDS)
		{
			Swap(Start, End);
		}
		const double U0 = Side > 0 ? Start : End;
		const double U1 = Side > 0 ? End : Start;

		bool bTopDown;
		switch (Section)
		{
		case ESection::Normal: bTopDown = (Flags & MRRoo::WF_NORMAL_TOPDOWN) != 0; break;
		case ESection::Below: bTopDown = (Flags & MRRoo::WF_BELOW_TOPDOWN) != 0; break;
		default: bTopDown = (Flags & MRRoo::WF_ABOVE_BOTTOMUP) == 0; break;
		}
		double Anchor;
		if (bTopDown)
		{
			Anchor = T0 == T1 ? T0 : FMath::CeilToDouble(FMath::Max(T0, T1) / RooPerSquare) * RooPerSquare;
		}
		else
		{
			Anchor = B0 == B1 ? B0 : FMath::FloorToDouble(FMath::Min(B0, B1) / RooPerSquare) * RooPerSquare;
		}
		auto V = [bTopDown, Anchor, YOff, RH](double Z)
		{
			return bTopDown ? (Anchor - Z - YOff) / RH : 1.0 - (YOff + Z - Anchor) / RH;
		};

		if (Section == ESection::Normal && (Flags & MRRoo::WF_NO_VTILE))
		{
			// drawn once: keep the part of the wall where 0 <= v <= 1 (fences, railings, hedges)
			auto Clip = [&V](double& B, double& T)
			{
				const double VB = V(B), VT = V(T);
				if (VT < 0)
				{
					T = VB != VT ? B + (T - B) * (VB - 0.0) / (VB - VT) : B;
				}
				if (VB > 1)
				{
					const double VT2 = V(T);
					B = VB != VT2 ? T - (T - B) * (1.0 - VT2) / (VB - VT2) : T;
				}
			};
			Clip(B0, T0);
			Clip(B1, T1);
		}
		UvB0 = FVector2D(U0, V(B0));
		UvT0 = FVector2D(U0, V(T0));
		UvB1 = FVector2D(U1, V(B1));
		UvT1 = FVector2D(U1, V(T1));
	}
}

int32 FMRRoomMesh::NumTriangles() const
{
	int32 N = 0;
	for (const FMRRoomMeshSection& S : Sections)
	{
		N += S.Triangles.Num() / 3;
	}
	return N;
}

FBox FMRRoomMesh::Bounds() const
{
	FBox B(ForceInit);
	for (const FMRRoomMeshSection& S : Sections)
	{
		for (const FVector& P : S.Positions)
		{
			B += P;
		}
	}
	return B;
}

FMRRoomMesh MRRoomMesh::Build(const FMRRooFile& Room, const FRepeat& Repeat, bool bCollision)
{
	FBuilder MB;
	const FHeights H{Room, bCollision};
	const FSectorLookup Sectors(Room);
	auto LightAt = [&Room, &Sectors](double X, double Y, double& Out)
	{
		const int32 S = Sectors.At(X, Y);
		if (!S)
		{
			return false;
		}
		Out = Room.Sectors[S - 1].Light / 255.0;
		return true;
	};

	// ---- floors and ceilings from the BSP leaves (convex polygons)
	for (const FMRRooNode& Node : Room.Nodes)
	{
		if (Node.Type != FMRRooNode::Leaf || !Node.Sector || Node.Points.Num() < 3 || !Room.Sector(Node.Sector))
		{
			continue;
		}
		const FMRRooSector& S = Room.Sectors[Node.Sector - 1];
		// every floor and ceiling texture tiles once per grid square, shifted by the sector's origin
		const double TX = S.TexX * Fine, TY = S.TexY * Fine;
		const double Level = S.Light / 255.0;
		TArray<FRoo> Floor;
		TArray<FVector2D> UVs;
		for (const FVector2f& P : Node.Points)
		{
			Floor.Add(FRoo(P.X, P.Y, H.Floor(Node.Sector, P.X, P.Y)));
			UVs.Add(FVector2D((P.X - TX) / RooPerSquare, (P.Y - TY) / RooPerSquare));
		}
		auto Normal = [](const TArray<FRoo>& Pts, FVector& N)
		{
			TArray<FVector> G;
			for (const FRoo& P : Pts)
			{
				G.Add(ToGltf(P));
			}
			return NewellNormal(G, N);
		};
		FVector N;
		if (Normal(Floor, N) && N.Y < 0)
		{
			Algo::Reverse(Floor);
			Algo::Reverse(UVs);
		}
		MB.Poly(S.FloorTexture, Floor, UVs, [Level](const FVector&) { return Level; });

		if (S.CeilingTexture)  // 0: open sky
		{
			TArray<FRoo> Ceil;
			TArray<FVector2D> CUVs;
			for (const FVector2f& P : Node.Points)
			{
				Ceil.Add(FRoo(P.X, P.Y, H.Ceil(Node.Sector, P.X, P.Y)));
				CUVs.Add(FVector2D((P.X - TX) / RooPerSquare, (P.Y - TY) / RooPerSquare));
			}
			if (Normal(Ceil, N) && N.Y > 0)
			{
				Algo::Reverse(Ceil);
				Algo::Reverse(CUVs);
			}
			MB.Poly(S.CeilingTexture, Ceil, CUVs, [Level](const FVector&) { return Level; });
		}
	}

	// ---- walls (Doom-style lower, upper and middle sections)
	TSet<FString> Seen;
	for (const FMRRooWall& W : Room.Walls)
	{
		// (Python's round() rounds halves to even)
		const FString Key = FString::Printf(TEXT("%lld,%lld,%lld,%lld,%d,%d"),
			static_cast<int64>(FMath::RoundHalfToEven(static_cast<double>(W.X0))), static_cast<int64>(FMath::RoundHalfToEven(static_cast<double>(W.Y0))),
			static_cast<int64>(FMath::RoundHalfToEven(static_cast<double>(W.X1))), static_cast<int64>(FMath::RoundHalfToEven(static_cast<double>(W.Y1))),
			W.PosSector, W.NegSector);
		if (Seen.Contains(Key))
		{
			continue;
		}
		Seen.Add(Key);
		const double Length = FMath::Sqrt(FMath::Square(static_cast<double>(W.X1) - W.X0) + FMath::Square(static_cast<double>(W.Y1) - W.Y0));
		if (Length < 1.0)
		{
			continue;
		}
		const FMRRooSidedef* SdPos = Room.Sidedef(W.PosSidedef);
		const FMRRooSidedef* SdNeg = Room.Sidedef(W.NegSidedef);
		const int32 P = Room.Sector(W.PosSector) ? W.PosSector : 0;
		const int32 N = Room.Sector(W.NegSector) ? W.NegSector : 0;
		if (!P && !N)
		{
			continue;
		}
		const FVector2D Mid((W.X0 + W.X1) / 2.0, (W.Y0 + W.Y1) / 2.0);
		double Fallback = 0.0;
		for (const int32 S : {P, N})
		{
			if (S)
			{
				Fallback = FMath::Max(Fallback, Room.Sectors[S - 1].Light / 255.0);
			}
		}
		auto WallLight = [&LightAt, Mid, Fallback](const FVector& Normal)
		{
			// the sector a wall face looks into: a little way off the wall along its normal (glTF x, z = ROO x, y)
			const double Step = 0.15 / MPerRoo;
			double Level;
			return LightAt(Mid.X + Normal.X * Step, Mid.Y + Normal.Z * Step, Level) ? Level : Fallback;
		};
		auto Emit = [&](uint16 Tex, double B0, double T0, double B1, double T1, const FMRRooSidedef* Sd, ESection Section, int32 Side)
		{
			if (T0 - B0 < 1 && T1 - B1 < 1)
			{
				return;
			}
			T0 = FMath::Max(T0, B0);
			T1 = FMath::Max(T1, B1);
			FVector2D UB0, UT0, UB1, UT1;
			WallUVs(Repeat(Tex), Sd, Section, Side, W, Length, B0, T0, B1, T1, UB0, UT0, UB1, UT1);
			if (T0 - B0 < 1 && T1 - B1 < 1)
			{
				return;
			}
			const FRoo P0(W.X0, W.Y0, B0), P1(W.X1, W.Y1, B1), P2(W.X1, W.Y1, T1), P3(W.X0, W.Y0, T0);
			MB.Poly(Tex, {P0, P1, P2, P3}, {UB0, UB1, UT1, UT0}, WallLight);
			MB.Poly(Tex, {P3, P2, P1, P0}, {UT0, UT1, UB1, UB0}, WallLight);
		};

		if (!P || !N)
		{
			// one-sided: a solid wall
			const int32 S = P ? P : N;
			const FMRRooSidedef* Sd = P ? SdPos : SdNeg;
			const int32 Side = P ? 1 : -1;
			if (!Sd)
			{
				Sd = SdPos ? SdPos : SdNeg;
			}
			Emit(Sd ? Sd->NormalTexture : 0, H.Floor(S, W.X0, W.Y0), H.Ceil(S, W.X0, W.Y0), H.Floor(S, W.X1, W.Y1), H.Ceil(S, W.X1, W.Y1),
				Sd, ESection::Normal, Side);
			continue;
		}

		const double FP0 = H.Floor(P, W.X0, W.Y0), FP1 = H.Floor(P, W.X1, W.Y1);
		const double FN0 = H.Floor(N, W.X0, W.Y0), FN1 = H.Floor(N, W.X1, W.Y1);
		const double CP0 = H.Ceil(P, W.X0, W.Y0), CP1 = H.Ceil(P, W.X1, W.Y1);
		const double CN0 = H.Ceil(N, W.X0, W.Y0), CN1 = H.Ceil(N, W.X1, W.Y1);
		const bool bSky = !Room.Sectors[P - 1].CeilingTexture && !Room.Sectors[N - 1].CeilingTexture;

		// lower section: the side with the lower floor sees it -> that side's "below" texture
		const bool bLowP = (FP0 + FP1) <= (FN0 + FN1);
		const FMRRooSidedef* SdLow = bLowP ? SdPos : SdNeg;
		Emit(SdLow ? SdLow->BelowTexture : 0, FMath::Min(FP0, FN0), FMath::Max(FP0, FN0), FMath::Min(FP1, FN1), FMath::Max(FP1, FN1),
			SdLow, ESection::Below, bLowP ? 1 : -1);

		// upper section (skipped between two open-sky sectors)
		if (!bSky)
		{
			const bool bHighP = (CP0 + CP1) >= (CN0 + CN1);
			const FMRRooSidedef* SdUp = bHighP ? SdPos : SdNeg;
			Emit(SdUp ? SdUp->AboveTexture : 0, FMath::Min(CP0, CN0), FMath::Max(CP0, CN0), FMath::Min(CP1, CN1), FMath::Max(CP1, CN1),
				SdUp, ESection::Above, bHighP ? 1 : -1);
		}

		// middle (fences, windows, railings): only where a normal texture is set
		if (bCollision && (!SdPos || (SdPos->Flags & MRRoo::WF_PASSABLE)) && (!SdNeg || (SdNeg->Flags & MRRoo::WF_PASSABLE)))
		{
			continue;
		}
		const TPair<const FMRRooSidedef*, int32> Sides[2] = {{SdPos, 1}, {SdNeg, -1}};
		for (const TPair<const FMRRooSidedef*, int32>& SS : Sides)
		{
			if (SS.Key && SS.Key->NormalTexture)
			{
				// WF_NO_VTILE (fences, hedges, railings) is clipped to one repeat in WallUVs
				Emit(SS.Key->NormalTexture, FMath::Max(FP0, FN0), FMath::Min(CP0, CN0), FMath::Max(FP1, FN1), FMath::Min(CP1, CN1),
					SS.Key, ESection::Normal, SS.Value);
				break;
			}
		}
	}
	return MoveTemp(MB.Mesh);
}

TArray<FMRRoomDepthArea> MRRoomMesh::DepthAreas(const FMRRooFile& Room)
{
	TArray<FMRRoomDepthArea> Out;
	for (const FMRRooNode& Node : Room.Nodes)
	{
		const FMRRooSector* S = Node.Type == FMRRooNode::Leaf && Node.Points.Num() >= 3 ? Room.Sector(Node.Sector) : nullptr;
		if (!S || !S->Depth())
		{
			continue;
		}
		FMRRoomDepthArea& A = Out.AddDefaulted_GetRef();
		A.Depth = S->Depth();
		for (const FVector2f& P : Node.Points)
		{
			A.Points.Add(FVector2D(P) * MRUnits::CmPerRoo);
		}
	}
	return Out;
}

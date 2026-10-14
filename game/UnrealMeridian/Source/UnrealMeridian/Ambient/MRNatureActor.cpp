#include "Ambient/MRNatureActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Core/MRUnits.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	/** Shards' fine units (1024 a square) -> cm */
	constexpr double SQ = MRUnits::CmPerSquare;
	constexpr double Fine = SQ / 1024.0;
	/** The atlas: 4 frames a row, 8 rows (make_nature_atlas.py) */
	constexpr int32 Cols = 4, Rows = 8;
	enum ERow : int32 { Butterfly = 0, FishRow = 1, Bird = 2, Bat = 3, Dot = 4 };
	/** flyers.ts BEAT: a wing beat's frames */
	constexpr int32 BeatFrames[4] = {0, 1, 2, 1};
	/** Floors more than this above or below the player's: no bats there (flyers.ts SAME_LEVEL, 1.2 squares) */
	constexpr double SameLevel = 1.2 * SQ;
	const TCHAR* MaterialPath = TEXT("/Game/Generated/Runtime/M_Nature.M_Nature");

	const FLinearColor ButterflyColours[] = {{1.f, 0.85f, 0.2f}, {1.f, 0.5f, 0.12f}, {0.95f, 0.95f, 0.88f}, {0.35f, 0.55f, 1.f}, {0.9f, 0.25f, 0.2f}, {0.75f, 0.45f, 0.9f}};
	const FLinearColor BirdColour(0.1f, 0.1f, 0.12f);
	const FLinearColor BatColour(0.29f, 0.23f, 0.19f);

	double Rnd(double Lo, double Hi) { return FMath::FRandRange(Lo, Hi); }
}

AMRNatureActor::AMRNatureActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Creatures"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->bUseAsyncCooking = false;
	Mesh->SetMobility(EComponentMobility::Movable);
	RootComponent = Mesh;
	Tags.Add(TEXT("MRAmbient"));
}

int32 AMRNatureActor::NumBirds() const
{
	int32 N = 0;
	for (const FFlock& F : Flocks)
	{
		N += F.Birds.Num();
	}
	return N;
}

void AMRNatureActor::Clear()
{
	Butterflies.Reset();
	Fish.Reset();
	Drops.Reset();
	Flocks.Reset();
	Bats.Reset();
	Water.Reset();
	WaterAt = FVector(1e9);
	if (Mesh)
	{
		Mesh->ClearAllMeshSections();
	}
}

bool AMRNatureActor::FloorAt(double X, double Y, double NearZ, double& OutZ) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRNatureFloor), false);
	FCollisionResponseParams WorldOnly;
	WorldOnly.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, FVector(X, Y, NearZ + 400.0), FVector(X, Y, NearZ - 900.0), ECC_WorldStatic, Params, WorldOnly))
	{
		OutZ = Hit.ImpactPoint.Z;
		return true;
	}
	return false;
}

bool AMRNatureActor::Roofed(const FVector& At) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRNatureRoof), false);
	FCollisionResponseParams WorldOnly;
	WorldOnly.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 60.0), At + FVector(0.0, 0.0, 4000.0), ECC_WorldStatic, Params, WorldOnly);
}

bool AMRNatureActor::IsWater(const FVector& Floor, double& OutSurface) const
{
	// a floor with a water texture (Raza's painted pond, rivers, the sea), not a wheat field or wet sand
	// with a depth: Shards takes every depth for water, and its fish leapt from the beach. The surface is
	// the floor as drawn: the collision floor is lowered by the depth's sink.
	const UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>();
	if (Zones && Zones->IsWaterAt(Floor))
	{
		OutSurface = Floor.Z + Zones->DepthSinkAt(Floor);
		return true;
	}
	return false;
}

bool AMRNatureActor::GroundSpot(const FVector& Feet, double Near, double Far, FVector& Out) const
{
	// a place on dry, open ground between Near and Far squares from us (nature.ts groundSpot)
	for (int32 K = 0; K < 10; ++K)
	{
		const double A = Rnd(0.0, UE_TWO_PI), R = Rnd(Near, Far) * SQ;
		const double X = Feet.X + FMath::Cos(A) * R, Y = Feet.Y + FMath::Sin(A) * R;
		double Z, Surface;
		if (!FloorAt(X, Y, Feet.Z, Z) || Roofed(FVector(X, Y, Z)) || IsWater(FVector(X, Y, Z), Surface))
		{
			continue;
		}
		Out = FVector(X, Y, Z);
		return true;
	}
	return false;
}

void AMRNatureActor::Step(float DeltaSeconds, const FVector& Feet, float FacingYaw, const FOptions& O)
{
	const double DtMs = FMath::Min<double>(DeltaSeconds * 1000.0, 100.0);
	const double S = DtMs / 1000.0;
	ViewerFloor = Feet.Z;
	if (!bMaterialSet)
	{
		bMaterialSet = true;
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
		{
			Mesh->SetMaterial(0, M);
		}
	}
	StepButterflies(S, Feet, O.Butterflies);
	StepFish(DtMs, Feet, FacingYaw, O.bFish);
	StepFlocks(S, Feet, O.Flocks);
	StepBats(DtMs, Feet, FacingYaw, O);
	Draw();
}

// ---- butterflies: fluttering low over the ground, turning often

void AMRNatureActor::StepButterflies(double S, const FVector& Feet, int32 Wanted)
{
	for (FButterfly& B : Butterflies)
	{
		if ((B.Turn -= S * 1000.0) <= 0.0)
		{
			B.Turn = Rnd(300.0, 1200.0);
			const bool bFar = FVector::Dist2D(B.S.Pos, Feet) > 8.0 * SQ;
			const double A = bFar ? FMath::Atan2(Feet.Y - B.S.Pos.Y, Feet.X - B.S.Pos.X) + Rnd(-1.0, 1.0) : Rnd(0.0, UE_TWO_PI);
			const double Speed = Rnd(0.5, 1.2) * SQ;
			B.V = FVector2D(FMath::Cos(A), FMath::Sin(A)) * Speed;
		}
		const double Nx = B.S.Pos.X + B.V.X * S, Ny = B.S.Pos.Y + B.V.Y * S;
		double Z, Surface;
		if (!FloorAt(Nx, Ny, B.Base, Z) || Roofed(FVector(Nx, Ny, Z)) || IsWater(FVector(Nx, Ny, Z), Surface))
		{
			B.V = -B.V;
		}
		else
		{
			B.S.Pos.X = Nx;
			B.S.Pos.Y = Ny;
			B.Base += (Z - B.Base) * FMath::Min(1.0, S * 3.0);
		}
		B.Bob += S;
		// low over the grass: about a sixth to two thirds of a square up
		B.S.Pos.Z = B.Base + SQ * (0.4 + 0.18 * FMath::Sin(B.Bob * 1.7) + 0.07 * FMath::Sin(B.Bob * 5.3));
		B.Phase = FMath::Fmod(B.Phase + B.Rate * S, 1.0);
		B.S.Frame = FMath::FloorToInt(B.Phase * 4.0) % 4;
	}
	Butterflies.RemoveAll([&Feet](const FButterfly& B) { return FVector::Dist2D(B.S.Pos, Feet) > 14.0 * SQ; });
	FVector At;
	if (Butterflies.Num() < Wanted && FMath::FRand() < S * 3.0 && GroundSpot(Feet, 3.0, 10.0, At))
	{
		FButterfly& B = Butterflies.AddDefaulted_GetRef();
		B.S.Row = Butterfly;
		B.S.Pos = At + FVector(0.0, 0.0, SQ * 0.4);
		B.S.Size = float(Rnd(150.0, 210.0) * Fine);
		B.S.Tint = ButterflyColours[FMath::RandRange(0, UE_ARRAY_COUNT(ButterflyColours) - 1)];
		B.S.bFlip = FMath::RandBool();
		B.Base = At.Z;
		B.Phase = FMath::FRand();
		B.Rate = Rnd(6.0, 9.0);
		B.Bob = Rnd(0.0, 10.0);
	}
	while (Butterflies.Num() > Wanted)
	{
		Butterflies.Pop();
	}
}

// ---- fish: now and then one leaps out of the water near us and splashes back

void AMRNatureActor::FindWater(const FVector& Feet)
{
	// open water (a water floor under open sky) between 3 and 14 squares off; a puddle or two is no place for fish
	Water.Reset();
	for (int32 K = 0; K < 160; ++K)
	{
		const double A = Rnd(0.0, UE_TWO_PI), R = Rnd(3.0, 14.0) * SQ;
		const double X = Feet.X + FMath::Cos(A) * R, Y = Feet.Y + FMath::Sin(A) * R;
		double Z, Surface;
		if (FloorAt(X, Y, Feet.Z, Z) && IsWater(FVector(X, Y, Z), Surface) && !Roofed(FVector(X, Y, Surface)))
		{
			Water.Emplace(X, Y, Surface);
		}
	}
	// about six squares of it at least (Shards: 24 half squares), from the share of the ring the samples found
	const double RingSquares = UE_DOUBLE_PI * (14.0 * 14.0 - 3.0 * 3.0);
	if (Water.Num() * RingSquares / 160.0 < 6.0)
	{
		Water.Reset();
	}
	WaterAt = Feet;
	WaterAge = 0.0;
}

void AMRNatureActor::StepFish(double DtMs, const FVector& Feet, float FacingYaw, bool bOn)
{
	const double S = DtMs / 1000.0;
	static IConsoleVariable* Showcase = IConsoleManager::Get().FindConsoleVariable(TEXT("mr.Ambient.Showcase"));
	const int32 ShowcaseLevel = Showcase ? Showcase->GetInt() : 0;
	for (FFish& F : Fish)
	{
		F.T = ShowcaseLevel >= 2 ? F.Duration * 0.5 : F.T + DtMs;
		const double K = FMath::Min(1.0, F.T / F.Duration);
		F.S.Pos = FVector(F.From.X + F.D.X * K, F.From.Y + F.D.Y * K, F.From.Z + F.Height * 4.0 * K * (1.0 - K) - 60.0 * Fine);
		F.S.Frame = K < 0.35 ? 0 : K < 0.65 ? 1 : 2;
		if (K >= 1.0)
		{
			Splash(FVector(F.S.Pos.X, F.S.Pos.Y, F.From.Z), 10);
		}
	}
	Fish.RemoveAll([](const FFish& F) { return F.T >= F.Duration; });
	for (FDrop& D : Drops)
	{
		D.Life -= DtMs;
		D.V.Z -= 5.0 * SQ * S;
		D.S.Pos += D.V * S;
		D.S.Pos.Z = FMath::Max(D.Surface, D.S.Pos.Z);
		D.S.Tint.A = float(FMath::Clamp(D.Life / 300.0, 0.0, 1.0));
	}
	Drops.RemoveAll([](const FDrop& D) { return D.Life <= 0.0; });
	if (!bOn)
	{
		return;
	}
	WaterAge += S;
	if (WaterAge > 4.0 && FVector::Dist2D(WaterAt, Feet) > 3.0 * SQ)
	{
		FindWater(Feet);
	}
	if (!Water.Num() || (NextJumpMs -= DtMs) > 0.0)
	{
		return;
	}
	NextJumpMs = ShowcaseLevel ? 400.0 : Rnd(3000.0, 12000.0);
	if (ShowcaseLevel >= 2 && Fish.Num() >= 3)
	{
		return;
	}
	FVector W = Water[FMath::RandRange(0, Water.Num() - 1)];
	if (ShowcaseLevel)
	{
		// in front of us
		const FVector Ahead = FRotator(0.f, FacingYaw, 0.f).Vector();
		for (int32 K = 0; K < 20 && FVector::DotProduct((W - Feet).GetSafeNormal2D(), Ahead) < 0.8; ++K)
		{
			W = Water[FMath::RandRange(0, Water.Num() - 1)];
		}
	}
	const double A = Rnd(0.0, UE_TWO_PI), Len = Rnd(0.4, 0.9) * SQ;
	FFish& F = Fish.AddDefaulted_GetRef();
	F.S.Row = FishRow;
	F.S.Pos = W;
	F.S.Size = float(Rnd(260.0, 360.0) * Fine);
	F.S.Tint = FLinearColor(0.72f, 0.78f, 0.82f);
	F.From = W;
	F.D = FVector2D(FMath::Cos(A), FMath::Sin(A)) * Len;
	F.Duration = Rnd(650.0, 950.0);
	F.Height = Rnd(0.35, 0.6) * SQ;
	// seen from where we look: heading right on screen or left (the picture faces right)
	const FVector Right = FRotator(0.f, FacingYaw, 0.f).RotateVector(FVector::RightVector);
	F.S.bFlip = F.D.X * Right.X + F.D.Y * Right.Y < 0.0;
	Splash(W, 6);
}

void AMRNatureActor::Splash(const FVector& At, int32 N)
{
	for (int32 K = 0; K < N && Drops.Num() < 60; ++K)
	{
		const double A = Rnd(0.0, UE_TWO_PI), Out = Rnd(0.2, 0.6) * SQ;
		FDrop& D = Drops.AddDefaulted_GetRef();
		D.S.Row = Dot;
		D.S.Pos = At + FVector(0.0, 0.0, 10.0 * Fine);
		D.S.Size = float(Rnd(40.0, 65.0) * Fine);
		D.S.Tint = FLinearColor(0.85f, 0.92f, 1.f);
		D.V = FVector(FMath::Cos(A) * Out, FMath::Sin(A) * Out, Rnd(0.8, 1.6) * SQ);
		D.Life = Rnd(450.0, 750.0);
		D.Surface = At.Z;
	}
}

// ---- birds: flocks crossing overhead; bats flitting

void AMRNatureActor::Beat(FFlyer& F, double S, double GlideChance)
{
	// beat the wings, or hold them level a while (a bird gliding now and then)
	if (F.Glide > 0.0)
	{
		F.Glide -= S * 1000.0;
		F.S.Frame = 1;
		return;
	}
	const double Before = F.Phase;
	F.Phase = FMath::Fmod(F.Phase + F.Rate * S, 1.0);
	if (F.Phase < Before && FMath::FRand() < GlideChance)
	{
		F.Glide = Rnd(800.0, 2500.0);
	}
	F.S.Frame = BeatFrames[FMath::FloorToInt(F.Phase * 4.0) % 4];
}

void AMRNatureActor::StepFlocks(double S, const FVector& Feet, int32 Wanted)
{
	for (FFlock& F : Flocks)
	{
		F.Pos += FVector(F.V.X, F.V.Y, 0.0) * S;
		for (FBird& B : F.Birds)
		{
			B.Bob += S;
			B.F.S.Pos = F.Pos + B.Offset + FVector(0.0, 0.0, FMath::Sin(B.Bob * 1.3) * 60.0 * Fine);
			Beat(B.F, S, 0.25);
		}
	}
	// gone once well past us, heading away
	Flocks.RemoveAll([&Feet](const FFlock& F)
	{
		const FVector2D D(F.Pos.X - Feet.X, F.Pos.Y - Feet.Y);
		return D.Size() >= 34.0 * SQ && D.X * F.V.X + D.Y * F.V.Y >= 0.0;
	});
	if (Flocks.Num() < Wanted && FMath::FRand() < S * 0.25)
	{
		// from far off, crossing near overhead
		FFlock& F = Flocks.AddDefaulted_GetRef();
		const double From = Rnd(0.0, UE_TWO_PI);
		F.Pos = FVector(Feet.X + FMath::Cos(From) * 30.0 * SQ, Feet.Y + FMath::Sin(From) * 30.0 * SQ, Feet.Z + Rnd(9.0, 15.0) * SQ);
		const FVector2D To(Feet.X + Rnd(-10.0, 10.0) * SQ, Feet.Y + Rnd(-10.0, 10.0) * SQ);
		const FVector2D Dir = (To - FVector2D(F.Pos.X, F.Pos.Y)).GetSafeNormal();
		F.V = Dir * Rnd(3.0, 5.0) * SQ;
		const int32 N = FMath::RandRange(2, 7);
		for (int32 K = 0; K < N; ++K)
		{
			FBird& B = F.Birds.AddDefaulted_GetRef();
			B.F.S.Row = Bird;
			B.F.S.Size = float(Rnd(450.0, 650.0) * Fine);
			B.F.S.Tint = BirdColour;
			B.F.Phase = FMath::FRand();
			B.F.Rate = Rnd(3.0, 4.5);
			B.F.Glide = Rnd(0.0, 2000.0);
			B.Offset = FVector(Rnd(-2.0, 2.0) * SQ, Rnd(-2.0, 2.0) * SQ, Rnd(-1.0, 1.0) * SQ);
			B.Bob = Rnd(0.0, 10.0);
			B.F.S.Pos = F.Pos + B.Offset;
		}
	}
	while (Flocks.Num() > Wanted)
	{
		Flocks.Pop();
	}
}

bool AMRNatureActor::BatHeight(double X, double Y, const FBat& B, bool bCave, double& OutZ) const
{
	// about the player's head in a cave, a little more under a high ceiling, never into it; over the floor outdoors
	double Floor;
	if (!FloorAt(X, Y, ViewerFloor, Floor))
	{
		return false;
	}
	if (!bCave)
	{
		if (Roofed(FVector(X, Y, Floor)))
		{
			return false;
		}
		OutZ = Floor + B.Below;
		return true;
	}
	if (FMath::Abs(Floor - ViewerFloor) > SameLevel)
	{
		return false;  // not over the tops of a maze's walls, nor a ledge high above us
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRNatureCeiling), false);
	FCollisionResponseParams WorldOnly;
	WorldOnly.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	FHitResult Hit;
	const double Ceiling = GetWorld()->LineTraceSingleByChannel(Hit, FVector(X, Y, Floor + 10.0), FVector(X, Y, Floor + 3000.0), ECC_WorldStatic, Params, WorldOnly)
		? Hit.ImpactPoint.Z : Floor + 3000.0;
	if (Ceiling - Floor < 0.9 * SQ)
	{
		return false;
	}
	OutZ = FMath::Min(Floor + B.Below, Ceiling - 0.2 * SQ);
	return true;
}

void AMRNatureActor::StepBats(double DtMs, const FVector& Feet, float FacingYaw, const FOptions& O)
{
	const double S = DtMs / 1000.0;
	for (FBat& B : Bats)
	{
		FFlyer& F = B.F;
		if ((B.Turn -= DtMs) <= 0.0)
		{
			// a sudden new heading, mostly back toward us when they stray
			B.Turn = Rnd(150.0, 600.0);
			const double Home = FMath::Atan2(Feet.Y - F.S.Pos.Y, Feet.X - F.S.Pos.X);
			const bool bFar = FVector::Dist2D(F.S.Pos, Feet) > 7.0 * SQ;
			const double A = bFar ? Home + Rnd(-0.8, 0.8) : Rnd(0.0, UE_TWO_PI);
			B.V = FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rnd(2.5, 5.0) * SQ;
		}
		const double Nx = F.S.Pos.X + B.V.X * S, Ny = F.S.Pos.Y + B.V.Y * S;
		double Z;
		if (!BatHeight(Nx, Ny, B, O.bCave, Z))
		{
			// a wall or the room's edge: turn back
			B.V = -B.V;
			B.Turn = Rnd(150.0, 400.0);
		}
		else
		{
			F.S.Pos.X = Nx;
			F.S.Pos.Y = Ny;
			F.S.Pos.Z += (Z - F.S.Pos.Z) * FMath::Min(1.0, S * 4.0);
		}
		Beat(F, S, 0.0);
	}
	Bats.RemoveAll([&Feet](const FBat& B) { return FVector::Dist2D(B.F.S.Pos, Feet) > 16.0 * SQ; });
	if (Bats.Num() < O.Bats && FMath::FRand() < S * 2.0)
	{
		// a bat about us, behind if it can be
		for (int32 K = 0; K < 12; ++K)
		{
			const double A = FMath::DegreesToRadians(FacingYaw) + UE_DOUBLE_PI + Rnd(-1.2, 1.2);
			const double R = Rnd(3.0, 9.0) * SQ;
			FBat B;
			B.F.S.Row = Bat;
			B.F.S.Size = float(Rnd(260.0, 380.0) * Fine);
			B.F.S.Tint = BatColour;
			B.F.Phase = FMath::FRand();
			B.F.Rate = Rnd(9.0, 13.0);
			// above the floor: head height in caves (the eyes are at three quarters of a square)
			B.Below = O.bCave ? Rnd(0.75, 1.3) * SQ : Rnd(2.0, 4.0) * SQ;
			const double X = Feet.X + FMath::Cos(A) * R, Y = Feet.Y + FMath::Sin(A) * R;
			double Z;
			if (BatHeight(X, Y, B, O.bCave, Z))
			{
				B.F.S.Pos = FVector(X, Y, Z);
				Bats.Add(B);
				break;
			}
		}
	}
	while (Bats.Num() > O.Bats)
	{
		Bats.Pop();
	}
}

// ---- drawing: every creature a quad turned to the camera

void AMRNatureActor::Draw()
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FRotator View = PC->PlayerCameraManager->GetCameraRotation();
	const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(View).GetUnitAxis(EAxis::Z);
	const FVector Normal = -FRotationMatrix(View).GetUnitAxis(EAxis::X);
	TArray<FVector> Pos;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Colours;
	const auto Put = [&](const FSprite& S)
	{
		if (S.Tint.A <= 0.f)
		{
			return;
		}
		const int32 Base = Pos.Num();
		const FVector R = Right * (S.Size * 0.5f), U = Up * (S.Size * 0.5f);
		Pos.Append({S.Pos - R + U, S.Pos + R + U, S.Pos + R - U, S.Pos - R - U});
		const float U0 = float(S.Frame + (S.bFlip ? 1 : 0)) / Cols, U1 = float(S.Frame + (S.bFlip ? 0 : 1)) / Cols;
		const float V0 = float(S.Row) / Rows, V1 = float(S.Row + 1) / Rows;
		UV.Append({FVector2D(U0, V0), FVector2D(U1, V0), FVector2D(U1, V1), FVector2D(U0, V1)});
		Normals.Append({Normal, Normal, Normal, Normal});
		Colours.Append({S.Tint, S.Tint, S.Tint, S.Tint});
		Tris.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
	};
	for (const FButterfly& B : Butterflies) Put(B.S);
	for (const FFish& F : Fish) Put(F.S);
	for (const FDrop& D : Drops) Put(D.S);
	for (const FFlock& F : Flocks)
	{
		for (const FBird& B : F.Birds) Put(B.F.S);
	}
	for (const FBat& B : Bats) Put(B.F.S);
	if (!Pos.Num())
	{
		Mesh->ClearAllMeshSections();
		return;
	}
	Mesh->CreateMeshSection_LinearColor(0, Pos, Tris, Normals, UV, Colours, TArray<FProcMeshTangent>(), false);
}

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRNatureActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Ambient life's small creatures (docs/adr/0013-ambient-life.md): Meridian Shards' Nature and Flyers
 * (packages/render/src/nature.ts and flyers.ts, ours), stepped the same way in world units:
 * - butterflies fluttering low over green country by day, not in the rain;
 * - fish leaping from open water now and then, with a splash of drops;
 * - flocks of birds crossing 9-15 squares overhead by day where there's open sky;
 * - bats flitting about head height in caves, dungeons, crypts and prisons (a few under the sky at night).
 * Our fireflies and falling leaves stay the environment's (ADR 0005). The pictures are Shards' shapes in
 * T_NatureAtlas (tools/textures/make_nature_atlas.py); every creature is a quad turned to the camera, all in
 * one procedural mesh rebuilt each frame (M_Nature: lit like the sprites, tinted by vertex colour).
 * Floors, roofs and water come from traces of our collision and the zone's depth areas.
 */
UCLASS(NotPlaceable, Transient)
class UNREALMERIDIAN_API AMRNatureActor : public AActor
{
	GENERATED_BODY()

public:
	AMRNatureActor();

	struct FOptions
	{
		int32 Butterflies = 0;
		bool bFish = false;
		int32 Flocks = 0;
		int32 Bats = 0;
		/** Bats under a cave's ceiling, else out under the sky */
		bool bCave = false;
	};
	/** One frame: the player's feet and facing (yaw degrees), and what should be about. */
	void Step(float DeltaSeconds, const FVector& Feet, float FacingYaw, const FOptions& Options);
	/** Everyone goes (a new room). */
	void Clear();
	/** How many are about (tests). */
	int32 NumButterflies() const { return Butterflies.Num(); }
	int32 NumBirds() const;
	int32 NumBats() const { return Bats.Num(); }
	int32 NumWaterSpots() const { return Water.Num(); }

private:
	struct FSprite
	{
		int32 Row = 0, Frame = 0;
		FVector Pos = FVector::ZeroVector;
		/** Size across, cm */
		float Size = 40.f;
		FLinearColor Tint = FLinearColor::White;
		bool bFlip = false;
	};
	struct FButterfly
	{
		FSprite S;
		FVector2D V = FVector2D::ZeroVector;
		double Turn = 0.0, Base = 0.0, Phase = 0.0, Rate = 7.0, Bob = 0.0, FloorAge = 0.0;
	};
	struct FFish
	{
		FSprite S;
		FVector From;
		FVector2D D;
		double T = 0.0, Duration = 800.0, Height = 100.0;
	};
	struct FDrop
	{
		FSprite S;
		FVector V;
		double Life = 600.0, Surface = 0.0;
	};
	struct FFlyer
	{
		FSprite S;
		double Phase = 0.0, Rate = 4.0, Glide = 0.0;
	};
	struct FBird
	{
		FFlyer F;
		FVector Offset;
		double Bob = 0.0;
	};
	struct FFlock
	{
		FVector Pos;
		FVector2D V;
		TArray<FBird> Birds;
	};
	struct FBat
	{
		FFlyer F;
		FVector2D V = FVector2D::ZeroVector;
		double Turn = 0.0, Below = 200.0;
	};

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh;
	TArray<FButterfly> Butterflies;
	TArray<FFish> Fish;
	TArray<FDrop> Drops;
	TArray<FFlock> Flocks;
	TArray<FBat> Bats;
	/** Open water about the player: its surface points, looked for now and then */
	TArray<FVector> Water;
	double WaterAge = 1000.0;
	FVector WaterAt = FVector(1e9);
	double NextJumpMs = 2000.0;
	double ViewerFloor = 0.0;
	bool bMaterialSet = false;

	/** The floor under XY near Z (world), or false */
	bool FloorAt(double X, double Y, double NearZ, double& OutZ) const;
	bool Roofed(const FVector& At) const;
	bool IsWater(const FVector& Floor, double& OutSurface) const;
	bool GroundSpot(const FVector& Feet, double Near, double Far, FVector& Out) const;
	void FindWater(const FVector& Feet);
	void StepButterflies(double S, const FVector& Feet, int32 Wanted);
	void StepFish(double DtMs, const FVector& Feet, float FacingYaw, bool bOn);
	void Splash(const FVector& At, int32 N);
	void StepFlocks(double S, const FVector& Feet, int32 Wanted);
	void StepBats(double DtMs, const FVector& Feet, float FacingYaw, const FOptions& O);
	bool BatHeight(double X, double Y, const FBat& B, bool bCave, double& OutZ) const;
	static void Beat(FFlyer& F, double S, double GlideChance);
	void Draw();
};

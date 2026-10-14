#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Character/MRSpriteAppearance.h"
#include "MRAmbientFigure.generated.h"

class UMRSpriteBodyComponent;
class UMRBgfSpriteComponent;
struct FMRBgf;
struct FMRNetAnimation;

/**
 * Someone or something of ambient life (docs/adr/0013-ambient-life.md): a townsperson, a critter, an arena
 * fighter. Only this client sees them and the server never hears of them. A plain actor, not a character:
 * UMRAmbientSubsystem moves it along its path (Place) with its feet on the floor, and the sprite body walks
 * by the velocity it's given. No collision; clicks, the crosshair and attacks pass to real objects.
 */
UCLASS(NotPlaceable, Transient)
class UNREALMERIDIAN_API AMRAmbientFigure : public AActor
{
	GENERATED_BODY()

public:
	AMRAmbientFigure();

	/** A person drawn as a player is (the sprite body, with a look's parts and colours). */
	void SetPerson(const FMRSpriteAppearance& A);
	/** A creature drawn with one of our monster sprites ("GiantRat"), at Scale of its size. */
	void SetMonster(FName Look, float Scale);
	/** A creature drawn from its own bitmap (online, or one with no sprite of ours), standing and moving. */
	void SetBitmap(TSharedPtr<const FMRBgf> Bgf, const FMRNetAnimation& Standing, const FMRNetAnimation& Moving, float Scale);
	/** Where it stands (its feet, world), which way it faces, and how fast it's going (cm/s: walking when not zero). */
	void Place(const FVector& Feet, float Yaw, const FVector& Velocity);
	/** An emote (player_actions.json: wave, point, dance...), or none to stop one that loops. */
	void PlayAction(FName Action);
	/** How visible, 0..1 (fading in and out). */
	void SetFade(float Fade);
	/** Over its head (world): where its speech goes. */
	FVector GetSpeechAnchor() const;
	FVector GetFeet() const { return Feet; }
	UMRSpriteBodyComponent* GetSpriteBody() const { return Body; }

	/** Which ambient object it draws (UMRAmbientSubsystem's id). */
	uint32 AmbientId = 0;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TObjectPtr<UMRSpriteBodyComponent> Body;
	UPROPERTY(Transient)
	TObjectPtr<UMRBgfSpriteComponent> Bitmap;
	FVector Feet = FVector::ZeroVector;
	float HeightCm = 180.f;
	float Opacity = 1.f;
	void EnsureBody();
};

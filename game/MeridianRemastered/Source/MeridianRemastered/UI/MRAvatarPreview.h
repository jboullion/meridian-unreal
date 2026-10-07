#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Character/MRSpriteAppearance.h"
#include "MRAvatarPreview.generated.h"

class UMRSpriteBodyComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * The player in the inventory dialog (docs/adr/0009-user-interface.md): a client-only actor far
 * below the world with its own sprite body (the player's look and colours, unlit) and an
 * orthographic scene capture that renders only that body into a render target. Turning it steps
 * through the original's eight view angles. Captures only while the dialog is open.
 */
UCLASS(NotPlaceable, Transient)
class MERIDIANREMASTERED_API AMRAvatarPreview : public AActor
{
	GENERATED_BODY()

public:
	AMRAvatarPreview();

	void SetAppearance(const FMRSpriteAppearance& Appearance);
	void SetCapturing(bool bCapture);
	void Turn(int32 Steps);
	UTextureRenderTarget2D* GetTarget() const { return Target; }

	/** Where it lives: well below every zone (they are on the Z = 0 plane). */
	static FVector Location() { return FVector(0.0, 0.0, -400000.0); }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UMRSpriteBodyComponent> Body;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	FMRSpriteAppearance Shown;
	bool bHasAppearance = false;
};

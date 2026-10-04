#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MRCharacterAppearance.generated.h"

class UAnimInstance;
class UMaterialInterface;
class USkeletalMesh;

/**
 * One visual piece of a character worn on top of the driver: hair, clothing, armour, or a
 * separate body/head mesh. Parts attach to the animation driver mesh or to each other.
 */
USTRUCT(BlueprintType)
struct FMRAppearancePart
{
	GENERATED_BODY()

	/** Unique within the appearance; other parts refer to it in AttachTo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** Own animation, if the part needs one. Unset = no anim instance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** Follow the AttachTo mesh's pose bone-for-bone (clothing, hair). Ignored if AnimClass is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bLeaderPose = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

	/** Part to attach to; "Driver" (or empty) is the character's animation driver mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName AttachTo = TEXT("Driver");

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName AttachSocket;

	/** Hidden from its owner in first person (hair, helmets). Still casts a shadow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bHideInFirstPerson = false;
};

/**
 * One head slider of the character creator. A slider value v in [-1, 1] drives the DecrMorph
 * morph target with -v when negative and the IncrMorph target with v when positive (one-ended
 * sliders have only IncrMorph). Morphs only move head vertices, so every character of a gender
 * shares one body mesh; a face is just a list of slider values.
 */
USTRUCT(BlueprintType)
struct FMRHeadSlider
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName DecrMorph;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName IncrMorph;
};

/**
 * What a character looks like, independent of what it is (stats, abilities live in GAS).
 *
 * The character's own mesh is the animation *driver*: a mesh on the UE5 mannequin skeleton
 * running DriverAnimClass. For MakeHuman characters the driver is the visible body itself
 * (tools/blender/mpfb_character.py fits it to the mannequin skeleton); hair, clothing and armour
 * are parts that follow it. See docs/characters.md.
 */
UCLASS(BlueprintType)
class MERIDIANREMASTERED_API UMRCharacterAppearance : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Mannequin-skeleton mesh that runs the animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver")
	TSoftObjectPtr<USkeletalMesh> DriverMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver")
	TSoftClassPtr<UAnimInstance> DriverAnimClass;

	/** Show the driver itself (MakeHuman bodies, the plain mannequin), or hide it under the parts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver")
	bool bDriverVisible = false;

	/**
	 * Bone hidden (with its children) on the owner's own body in first person: on the driver if
	 * it is visible, and on FirstPersonBodyPart. Hiding collapses the bone, which stretches any
	 * vertices partly weighted to it, so hide as little as possible: the head, not the neck.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driver")
	FName FirstPersonHiddenBone = TEXT("head");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts")
	TArray<FMRAppearancePart> Parts;

	/** Name of the part, if any, that also hides FirstPersonHiddenBone in first person (a separate body mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts")
	FName FirstPersonBodyPart;

	/** Head sliders, in creator order (generated from the MakeHuman kit's manifest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Creator")
	TArray<FMRHeadSlider> HeadSliders;

	/** Hairstyles that fit this body; each carries the same head morphs. Index 0 is the default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Creator")
	TArray<TSoftObjectPtr<USkeletalMesh>> HairStyles;
};

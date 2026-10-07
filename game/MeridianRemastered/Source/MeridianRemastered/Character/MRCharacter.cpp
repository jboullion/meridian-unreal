#include "Character/MRCharacter.h"

#include "AbilitySystemComponent.h"
#include "Abilities/MRAttributeSet.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimInstance.h"
#include "AnimationRuntime.h"
#include "Character/MRCharacterAppearance.h"
#include "Character/MRCharacterMovementComponent.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Character/MRSpriteData.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "Misc/App.h"
#include "EngineUtils.h"
#include "Monsters/MRMonster.h"
#include "HAL/FileManager.h"
#include "HighResScreenshot.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Player/MRPlayerState.h"
#include "UObject/ConstructorHelpers.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	// Engine mannequin pack, copied into the project by tools/setup.ps1 (not committed).
	const TCHAR* MannequinMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* MannequinAnimPath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C");
	const FName DriverPartName(TEXT("Driver"));

	TAutoConsoleVariable<float> CVarThirdPersonPitch(TEXT("mr.Camera.ThirdPersonPitch"), 20.f,
		TEXT("How far (degrees) the third-person cameras may tilt up or down from eye level (0 = locked at eye level)."));

	float ThirdPersonPitchLimit() { return FMath::Clamp(CVarThirdPersonPitch.GetValueOnGameThread(), 0.f, 89.f); }

	TAutoConsoleVariable<int32> CVarSpriteBody(TEXT("mr.Character.SpriteBody"), 0,
		TEXT("1: characters spawned from now on are drawn as original-style sprites (docs/sprites.md)."));
	// test keys 1-4 (OnEmote)
	const FName EmoteKeys[] = {TEXT("wave"), TEXT("point"), TEXT("dance"), TEXT("cast")};
}

AMRCharacter::AMRCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMRCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	// ~1.8 m tall human; eye at ~1.65 m above the floor (the original's 0.75-square eye height)
	GetCapsuleComponent()->InitCapsuleSize(34.f, 90.f);
	BaseEyeHeight = 75.f;   // relative to the capsule centre (90 cm above the floor)
	CrouchedEyeHeight = 35.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 12.f;
	CameraBoom->bEnableCameraLag = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(90.f);

	// The character mesh is the animation driver: mannequin skeleton, feet on the capsule bottom,
	// facing +X (mannequin assets face +Y).
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(0.68f, 0.68f, 1.8f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(Cylinder.Object);
	}

	// Iris filters replication spatially by this distance. Zones sit 2 km apart, so players in
	// other zones are never sent; 300 m covers the largest outdoor zone (Raza is ~150 m across).
	SetNetCullDistanceSquared(FMath::Square(30000.f));

	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	ApplyViewMode();
}

UAbilitySystemComponent* AMRCharacter::GetAbilitySystemComponent() const
{
	const AMRPlayerState* PS = GetPlayerState<AMRPlayerState>();
	return PS ? PS->GetAbilitySystemComponent() : nullptr;
}

UMRAttributeSet* AMRCharacter::GetAttributeSet() const
{
	const AMRPlayerState* PS = GetPlayerState<AMRPlayerState>();
	return PS ? PS->GetAttributeSet() : nullptr;
}

UMRCharacterMovementComponent* AMRCharacter::GetMRMovement() const
{
	return Cast<UMRCharacterMovementComponent>(GetCharacterMovement());
}

void AMRCharacter::BeginPlay()
{
	Super::BeginPlay();

	// -MRAppearance=/Game/Path/DA_Name overrides the configured appearance (testing)
	FSoftObjectPath AppearancePath = DefaultAppearance;
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("MRAppearance="), Override))
	{
		AppearancePath = FSoftObjectPath(Override.Contains(TEXT(".")) ? Override
			: Override + TEXT(".") + FPackageName::GetShortName(Override));
	}
	const UMRCharacterAppearance* Appearance = nullptr;
	if (AppearancePath.IsValid())
	{
		Appearance = Cast<UMRCharacterAppearance>(AppearancePath.TryLoad());
		if (!Appearance)
		{
			UE_LOG(LogMeridian, Log, TEXT("Appearance %s not found (see tools/ue/import_character.ps1); using fallbacks"),
				*AppearancePath.ToString());
		}
	}
	bUseSpriteBody = bSpriteBody || CVarSpriteBody.GetValueOnGameThread() != 0 || FParse::Param(FCommandLine::Get(), TEXT("MRSpriteBody"));
	if (HasAuthority() && SpriteAppearance.Look.IsNone())
	{
		SpriteAppearance.Look = DefaultSpriteLook;  // replicated: every client draws this look
	}
	if (bUseSpriteBody)
	{
		ApplySpriteBody(SpriteAppearance.Look.IsNone() ? DefaultSpriteLook : SpriteAppearance.Look);
	}
	else
	{
		ApplyAppearance(Appearance);
	}
	ApplyViewMode();
}

void AMRCharacter::SetSpriteHeight(float Scale)
{
	FMRSpriteAppearance A = SpriteAppearance;
	A.HeightPct = FMath::RoundToInt(FMath::Clamp(Scale, 0.5f, 1.5f) * 100.f);
	SetSpriteAppearance(A);
}

// ------------------------------------------------------------------------- sprite sync

void AMRCharacter::SetSpriteAppearance(const FMRSpriteAppearance& NewAppearance)
{
	SpriteAppearance = NewAppearance;
	ApplySpriteAppearance();  // predicted here
	if (!HasAuthority())
	{
		ServerSetSpriteAppearance(NewAppearance);
	}
}

void AMRCharacter::ServerSetSpriteAppearance_Implementation(const FMRSpriteAppearance& NewAppearance)
{
	FMRSpriteAppearance A = NewAppearance;
	if (!FMRSpriteLibrary::Get().Looks.Contains(A.Look))
	{
		A.Look = SpriteAppearance.Look;  // only looks the game knows
	}
	A.Skin = FMath::Clamp(A.Skin, -1, FMRSpriteColours::NumSkins - 1);
	A.Hair = FMath::Clamp(A.Hair, -1, FMRSpriteColours::NumHair - 1);
	A.Shirt = FMath::Clamp(A.Shirt, -1, FMRSpriteColours::NumClothes - 1);
	A.Pants = FMath::Clamp(A.Pants, -1, FMRSpriteColours::NumClothes - 1);
	A.HeightPct = FMath::Clamp(A.HeightPct, 90, 110);  // variety, never a game advantage
	SpriteAppearance = A;
	ApplySpriteAppearance();
}

void AMRCharacter::OnRep_SpriteAppearance()
{
	ApplySpriteAppearance();
}

void AMRCharacter::ApplySpriteAppearance()
{
	const FMRSpriteAppearance& A = SpriteAppearance;
	if (SpriteBody)
	{
		if (!A.Look.IsNone() && SpriteBody->GetLook() != A.Look)
		{
			SpriteBody->SetLook(A.Look);
			AppearanceDescription = FString::Printf(TEXT("sprite:%s"), *A.Look.ToString());
		}
		SpriteBody->SetColours(A.Skin, A.Hair, A.Shirt, A.Pants);
	}
	if (bUseSpriteBody)
	{
		ApplySpriteHeight(A.HeightPct / 100.f);
	}
}

void AMRCharacter::ApplyCommandLineAppearance()
{
	if (bAppliedCommandLineAppearance || !IsLocallyControlled())
	{
		return;
	}
	bAppliedCommandLineAppearance = true;
	FMRSpriteAppearance A = SpriteAppearance;
	FString Value;
	if (FParse::Value(FCommandLine::Get(), TEXT("MRSpriteLook="), Value))
	{
		A.Look = FName(*Value);
	}
	float Height = 1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("MRSpriteHeight="), Height))
	{
		A.HeightPct = FMath::RoundToInt(Height * 100.f);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("MRSpriteColours="), Value))
	{
		TArray<FString> C;
		Value.ParseIntoArray(C, TEXT(","));
		int32* Fields[] = {&A.Skin, &A.Hair, &A.Shirt, &A.Pants};
		for (int32 i = 0; i < 4 && i < C.Num(); ++i)
		{
			*Fields[i] = FCString::Atoi(*C[i]);
		}
	}
	if (A != SpriteAppearance)
	{
		SetSpriteAppearance(A);
	}
}

void AMRCharacter::PlaySpriteAction(FName Action)
{
	if (SpriteBody)
	{
		if (Action.IsNone())
		{
			SpriteBody->StopAction();
		}
		else
		{
			SpriteBody->PlayAction(Action);  // predicted here
		}
	}
	if (HasAuthority())
	{
		ServerPlaySpriteAction_Implementation(Action);
	}
	else
	{
		ServerPlaySpriteAction(Action);
	}
}

void AMRCharacter::ServerPlaySpriteAction_Implementation(FName Action)
{
	if (!Action.IsNone() && !FMRSpriteLibrary::Get().Actions.Contains(Action))
	{
		return;
	}
	SpriteAction.Action = Action;
	SpriteAction.Seq++;
	SpriteAction.ServerTime = GetWorld()->GetTimeSeconds();
	if (Action == TEXT("fist_attack") || Action == TEXT("weapon_attack"))
	{
		// placeholder until combat: the nearest monster in front, within reach, takes the hit
		AMRMonster* Best = nullptr;
		float BestDist = 260.f;
		for (TActorIterator<AMRMonster> It(GetWorld()); It; ++It)
		{
			const FVector To = It->GetActorLocation() - GetActorLocation();
			const float Dist = To.Size2D() - It->GetCapsuleComponent()->GetScaledCapsuleRadius();
			if (!It->IsDead() && !It->IsNpc() && Dist < BestDist
				&& (To.GetSafeNormal2D() | GetActorForwardVector().GetSafeNormal2D()) > 0.4f)
			{
				Best = *It;
				BestDist = Dist;
			}
		}
		if (Best)
		{
			Best->TakePlaceholderHit(this);
		}
	}
	if (!IsLocallyControlled())
	{
		OnRep_SpriteAction();  // a listen server draws the others too
	}
}

void AMRCharacter::OnRep_SpriteAction()
{
	if (IsLocallyControlled() || !SpriteBody)
	{
		return;  // the owner played it already
	}
	if (SpriteAction.Action.IsNone())
	{
		SpriteBody->StopAction();
		return;
	}
	// a late joiner skips one-shots that ended long ago (loops like the dance still show)
	const FMRSpriteAction* Def = FMRSpriteLibrary::Get().Actions.Find(SpriteAction.Action);
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const float Age = GS ? GS->GetServerWorldTimeSeconds() - SpriteAction.ServerTime : 0.f;
	if (Def && Def->OnceLengthMs() > 0 && Age > Def->OnceLengthMs() / 1000.f + 1.f)
	{
		return;
	}
	SpriteBody->PlayAction(SpriteAction.Action);
}

void AMRCharacter::ApplySpriteHeight(float Scale)
{
	if (SpriteBody)
	{
		SpriteBody->SetHeightScale(Scale);
		Scale = SpriteBody->GetHeightScale();
	}
	// the eyes move with the drawing: 1.65 m above the floor at scale 1 (the capsule centre is 90 cm up)
	FirstPersonEye = FVector(12.f, 0.f, BaseEyeHeight + 165.f * (Scale - 1.f));
	ApplyViewMode();
}

void AMRCharacter::ApplySpriteBody(FName Look)
{
	ClearAppearance();
	CurrentAppearance = nullptr;
	bUseSpriteBody = true;
	GetMesh()->SetSkeletalMesh(nullptr);
	GetMesh()->SetVisibility(false);
	PlaceholderBody->SetVisibility(false);
	AppearanceDescription = FString::Printf(TEXT("sprite:%s"), *Look.ToString());
	UpdateEyePosition();
	if (IsNetMode(NM_DedicatedServer) || !FApp::CanEverRender())
	{
		return;  // nothing is drawn on a server (or a -nullrhi test client)
	}
	if (!SpriteBody)
	{
		SpriteBody = NewObject<UMRSpriteBodyComponent>(this, TEXT("SpriteBody"));
		SpriteBody->SetupAttachment(GetCapsuleComponent());
		SpriteBody->RegisterComponent();
	}
	SpriteBody->SetLook(Look);
	if (Look == SpriteAppearance.Look)
	{
		ApplySpriteAppearance();
	}
	ApplyFirstPersonVisibility();
	UE_LOG(LogMeridian, Log, TEXT("%s appearance: %s"), *GetName(), *AppearanceDescription);
}

// ------------------------------------------------------------------------------ appearance

void AMRCharacter::ClearAppearance()
{
	for (TPair<FName, TObjectPtr<USceneComponent>>& Part : AppearanceParts)
	{
		if (Part.Value)
		{
			Part.Value->DestroyComponent();
		}
	}
	AppearanceParts.Reset();
	FirstPersonHiddenParts.Reset();
	FirstPersonBodyPart = NAME_None;
	DriverHiddenBone = NAME_None;
	BodyPartHiddenBone = NAME_None;
}

bool AMRCharacter::ApplyMannequinFallback()
{
	USkeletalMesh* MannequinMesh = LoadObject<USkeletalMesh>(nullptr, MannequinMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UClass* Anim = LoadClass<UAnimInstance>(nullptr, MannequinAnimPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!MannequinMesh || !Anim)
	{
		return false;
	}
	GetMesh()->SetSkeletalMesh(MannequinMesh);
	GetMesh()->SetAnimInstanceClass(Anim);
	GetMesh()->SetVisibility(true);
	DriverHiddenBone = TEXT("head");
	AppearanceDescription = TEXT("mannequin");
	return true;
}

USceneComponent* AMRCharacter::CreateAppearancePart(const FMRAppearancePart& Part, USceneComponent* Parent)
{
	{
		USkeletalMesh* PartMesh = Part.Mesh.LoadSynchronous();
		if (!PartMesh)
		{
			return nullptr;
		}
		USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(this, Part.Name);
		Comp->SetSkeletalMesh(PartMesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (int32 i = 0; i < Part.MaterialOverrides.Num(); ++i)
		{
			if (UMaterialInterface* Mat = Part.MaterialOverrides[i].LoadSynchronous())
			{
				Comp->SetMaterial(i, Mat);
			}
		}
		Comp->SetupAttachment(Parent, Part.AttachSocket);
		Comp->RegisterComponent();
		if (UClass* Anim = Part.AnimClass.LoadSynchronous())
		{
			Comp->SetAnimInstanceClass(Anim);
		}
		else if (Part.bLeaderPose)
		{
			if (USkinnedMeshComponent* Leader = Cast<USkinnedMeshComponent>(Parent))
			{
				Comp->SetLeaderPoseComponent(Leader);
			}
		}
		return Comp;
	}
}

void AMRCharacter::ApplyHeadSliders(const TArray<float>& Values)
{
	if (!CurrentAppearance)
	{
		return;
	}
	TArray<USkinnedMeshComponent*> Meshes;
	GetComponents<USkinnedMeshComponent>(Meshes);
	const TArray<FMRHeadSlider>& Sliders = CurrentAppearance->HeadSliders;
	for (int32 i = 0; i < Sliders.Num(); ++i)
	{
		const float V = Values.IsValidIndex(i) ? FMath::Clamp(Values[i], -1.f, 1.f) : 0.f;
		for (USkinnedMeshComponent* Comp : Meshes)
		{
			USkeletalMeshComponent* Skel = Cast<USkeletalMeshComponent>(Comp);
			if (!Skel)
			{
				continue;
			}
			if (!Sliders[i].DecrMorph.IsNone())
			{
				Skel->SetMorphTarget(Sliders[i].DecrMorph, FMath::Max(0.f, -V));
			}
			if (!Sliders[i].IncrMorph.IsNone())
			{
				Skel->SetMorphTarget(Sliders[i].IncrMorph, FMath::Max(0.f, V));
			}
		}
	}
}

void AMRCharacter::ApplyRandomHeadSliders(int32 Seed, float Strength)
{
	if (!CurrentAppearance)
	{
		return;
	}
	FRandomStream Rng(Seed);
	TArray<float> Values;
	for (int32 i = 0; i < CurrentAppearance->HeadSliders.Num(); ++i)
	{
		Values.Add(Rng.FRandRange(-Strength, Strength));
	}
	ApplyHeadSliders(Values);
}

void AMRCharacter::ApplyAppearance(const UMRCharacterAppearance* Appearance)
{
	ClearAppearance();
	CurrentAppearance = Appearance;
	USkeletalMeshComponent* Driver = GetMesh();
	const bool bDedicatedServer = IsNetMode(NM_DedicatedServer);

	bool bApplied = false;
	if (Appearance)
	{
		USkeletalMesh* DriverMesh = Appearance->DriverMesh.LoadSynchronous();
		UClass* DriverAnim = Appearance->DriverAnimClass.LoadSynchronous();
		if (DriverMesh && DriverAnim)
		{
			Driver->SetSkeletalMesh(DriverMesh);
			Driver->SetAnimInstanceClass(DriverAnim);
			Driver->SetVisibility(Appearance->bDriverVisible);
			DriverHiddenBone = Appearance->bDriverVisible ? Appearance->FirstPersonHiddenBone : NAME_None;
			FirstPersonBodyPart = Appearance->FirstPersonBodyPart;
			BodyPartHiddenBone = Appearance->FirstPersonHiddenBone;
			AppearanceDescription = FString::Printf(TEXT("appearance:%s"), *Appearance->GetName());
			bApplied = true;

			// Cosmetic parts are pointless on a dedicated server: it only needs the driver's pose.
			if (!bDedicatedServer)
			{
				for (const FMRAppearancePart& Part : Appearance->Parts)
				{
					USceneComponent* Parent = Driver;
					if (!Part.AttachTo.IsNone() && Part.AttachTo != DriverPartName)
					{
						if (const TObjectPtr<USceneComponent>* Found = AppearanceParts.Find(Part.AttachTo))
						{
							Parent = *Found;
						}
						else
						{
							UE_LOG(LogMeridian, Warning, TEXT("%s: part %s attaches to unknown part %s (list parents first)"),
								*Appearance->GetName(), *Part.Name.ToString(), *Part.AttachTo.ToString());
						}
					}
					if (USceneComponent* Created = CreateAppearancePart(Part, Parent))
					{
						AppearanceParts.Add(Part.Name, Created);
						if (Part.bHideInFirstPerson)
						{
							FirstPersonHiddenParts.Add(Part.Name);
						}
					}
					else
					{
						UE_LOG(LogMeridian, Warning, TEXT("%s: part %s has no loadable asset"),
							*Appearance->GetName(), *Part.Name.ToString());
					}
				}
			}
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("Appearance %s is missing its driver mesh or anim class"), *Appearance->GetName());
		}
	}

	if (!bApplied)
	{
		CurrentAppearance = nullptr; // fallbacks have no head sliders
		bApplied = ApplyMannequinFallback();
	}
	if (!bApplied)
	{
		AppearanceDescription = TEXT("placeholder");
	}

	// The driver must keep animating while hidden, because visible parts follow its pose.
	// On a dedicated server nothing is rendered; only montages need to run for now.
	Driver->VisibilityBasedAnimTickOption = bDedicatedServer
		? EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered
		: EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	PlaceholderBody->SetVisibility(!bApplied);
	UpdateEyePosition();
	ApplyViewMode();
	UE_LOG(LogMeridian, Log, TEXT("%s appearance: %s (%d parts)"), *GetName(), *AppearanceDescription, AppearanceParts.Num());
}

void AMRCharacter::UpdateEyePosition()
{
	// Default: the original game's eye height (0.75 squares = 1.65 m) when there is no skeleton.
	FirstPersonEye = FVector(12.f, 0.f, BaseEyeHeight);
	const USkeletalMeshComponent* Driver = GetMesh();
	const USkeletalMesh* DriverMesh = Driver ? Driver->GetSkeletalMeshAsset() : nullptr;
	if (DriverMesh)
	{
		const FReferenceSkeleton& RefSkeleton = DriverMesh->GetRefSkeleton();
		const int32 Head = RefSkeleton.FindBoneIndex(TEXT("head"));
		if (Head != INDEX_NONE)
		{
			const FVector HeadCS = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, Head).GetLocation();
			const FVector HeadActor = Driver->GetRelativeTransform().TransformPosition(HeadCS);
			FirstPersonEye = HeadActor + EyeOffsetFromHeadBone;
			UE_LOG(LogMeridian, Log, TEXT("%s first-person eyes %.0f cm above the floor"), *GetName(),
				FirstPersonEye.Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		}
	}
}

void AMRCharacter::ApplyFirstPersonVisibility()
{
	// Only the player's own view changes: everyone else always sees the whole character.
	const bool bFirstPerson = IsFirstPerson();
	const bool bHideOwnHead = bFirstPerson && IsLocallyControlled();
	if (SpriteBody)
	{
		SpriteBody->SetOwnerNoSee(bFirstPerson);  // the shadow card still casts
	}

	auto SetHeadBone = [bHideOwnHead](USkinnedMeshComponent* Comp, FName Bone)
	{
		if (!Comp || Bone.IsNone() || Comp->GetBoneIndex(Bone) == INDEX_NONE)
		{
			return;
		}
		if (bHideOwnHead)
		{
			Comp->HideBoneByName(Bone, PBO_None);
		}
		else
		{
			Comp->UnHideBoneByName(Bone);
		}
	};
	SetHeadBone(GetMesh(), DriverHiddenBone);
	if (const TObjectPtr<USceneComponent>* Body = AppearanceParts.Find(FirstPersonBodyPart))
	{
		SetHeadBone(Cast<USkinnedMeshComponent>(Body->Get()), BodyPartHiddenBone);
	}
	for (const FName& Name : FirstPersonHiddenParts)
	{
		if (const TObjectPtr<USceneComponent>* Part = AppearanceParts.Find(Name))
		{
			if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Part->Get()))
			{
				Prim->SetOwnerNoSee(bFirstPerson);
				Prim->bCastHiddenShadow = true; // keep the full shadow on the ground
			}
		}
	}
}

void AMRCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMRCharacter, ViewMode);
	DOREPLIFETIME(AMRCharacter, SpriteAppearance);
	DOREPLIFETIME(AMRCharacter, SpriteAction);
}

// ------------------------------------------------------------------------- abilities

void AMRCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitAbilityActorInfo(); // server
}

void AMRCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitAbilityActorInfo(); // client
}

void AMRCharacter::InitAbilityActorInfo()
{
	if (AMRPlayerState* PS = GetPlayerState<AMRPlayerState>())
	{
		PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);
	}
}

// ---------------------------------------------------------------------------- tick

void AMRCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled() && !IsFirstPerson())
	{
		ClampThirdPersonPitch();
	}
	if (HasAuthority())
	{
		ServerTickVigor(DeltaSeconds);
		ServerTickZone(DeltaSeconds);
	}
}

void AMRCharacter::ServerTickVigor(float DeltaSeconds)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	const UMRAttributeSet* Attr = GetAttributeSet();
	const UMRCharacterMovementComponent* Move = GetMRMovement();
	if (!ASC || !Attr || !Move)
	{
		return;
	}
	const float Delta = Move->IsSprinting() ? -SprintVigorPerSecond * DeltaSeconds : VigorRegenPerSecond * DeltaSeconds;
	const float NewVigor = FMath::Clamp(Attr->GetVigor() + Delta, 0.f, Attr->GetMaxVigor());
	if (!FMath::IsNearlyEqual(NewVigor, Attr->GetVigor()))
	{
		ASC->SetNumericAttributeBase(UMRAttributeSet::GetVigorAttribute(), NewVigor);
	}
}

void AMRCharacter::ServerTickZone(float DeltaSeconds)
{
	ZoneCheckAccumulator += DeltaSeconds;
	if (ZoneCheckAccumulator < 0.2f)
	{
		return;
	}
	ZoneCheckAccumulator = 0.f;
	if (UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>())
	{
		Zones->UpdatePawnZone(this);
	}
}

// ------------------------------------------------------------------------ view mode

void AMRCharacter::SetFirstPerson(bool bNewFirstPerson)
{
	SetViewMode(bNewFirstPerson ? EMRViewMode::FirstPerson : EMRViewMode::Chase);
}

void AMRCharacter::SetViewMode(EMRViewMode NewMode)
{
	if (ViewMode == NewMode)
	{
		return;
	}
	const bool bWasFixed = ViewMode == EMRViewMode::Behind || ViewMode == EMRViewMode::Front;
	if (!bWasFixed && (NewMode == EMRViewMode::Behind || NewMode == EMRViewMode::Front) && Controller)
	{
		// the fixed views steer with the controller's yaw: start from where the body faces
		FRotator Rot = Controller->GetControlRotation();
		Rot.Yaw = GetActorRotation().Yaw;
		Controller->SetControlRotation(Rot);
		FixedViewPitch = FixedPitch;
		FixedViewArm = FixedArmLength;
	}
	ViewMode = NewMode;
	if (NewMode != EMRViewMode::FirstPerson && Controller)
	{
		Controller->SetControlRotation(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f));  // eye level
	}
	ApplyViewMode(); // predict locally
	if (!HasAuthority())
	{
		ServerSetViewMode(NewMode);
	}
}

void AMRCharacter::ServerSetViewMode_Implementation(EMRViewMode NewMode)
{
	ViewMode = NewMode;
	ApplyViewMode();
}

void AMRCharacter::OnRep_ViewMode()
{
	ApplyViewMode();
}

void AMRCharacter::ApplyViewMode()
{
	const bool bFirstPerson = IsFirstPerson();
	const bool bFixed = ViewMode == EMRViewMode::Behind || ViewMode == EMRViewMode::Front;
	// Rotation rules must match on client and server or movement will be corrected.
	bUseControllerRotationYaw = bFirstPerson || bFixed;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = ViewMode == EMRViewMode::Chase;
		Move->RotationRate = FRotator(0.f, 540.f, 0.f);
	}
	if (CameraBoom)
	{
		// The boom pivots at eye height in every view; first person puts the camera at the eyes,
		// the others swing it out from there. The fixed views hang it off the body, not the mouse.
		CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, FirstPersonEye.Z));
		CameraBoom->bUsePawnControlRotation = !bFixed;
		CameraBoom->bInheritPitch = true;  // third person: limited to mr.Camera.ThirdPersonPitch (OnLook, Tick)
		if (bFixed)
		{
			CameraBoom->SetRelativeRotation(FRotator(FixedViewPitch, ViewMode == EMRViewMode::Front ? 180.f : 0.f, 0.f));
			CameraBoom->TargetArmLength = FixedViewArm;
			CameraBoom->SocketOffset = FVector::ZeroVector;
		}
		else
		{
			CameraBoom->TargetArmLength = bFirstPerson ? 0.f : ThirdPersonArmLength;
			CameraBoom->SocketOffset = bFirstPerson ? FVector(FirstPersonEye.X, FirstPersonEye.Y, 0.f) : FVector(0.f, 45.f, 25.f);
		}
	}
	if (PlaceholderBody)
	{
		PlaceholderBody->SetOwnerNoSee(bFirstPerson);
	}
	ApplyFirstPersonVisibility();
}

// ---------------------------------------------------------------------------- input

void AMRCharacter::BuildInput()
{
	if (DefaultContext)
	{
		return;
	}
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, FName(Name));
		A->ValueType = Type;
		return A;
	};
	MoveAction = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	SprintAction = MakeAction(TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	WalkAction = MakeAction(TEXT("IA_Walk"), EInputActionValueType::Boolean);
	ViewAction = MakeAction(TEXT("IA_ToggleView"), EInputActionValueType::Boolean);
	ZoomAction = MakeAction(TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	CrouchAction = MakeAction(TEXT("IA_Crouch"), EInputActionValueType::Boolean);

	DefaultContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Default"));
	UInputMappingContext* Ctx = DefaultContext;

	// WASD -> Axis2D (X = right, Y = forward)
	{
		FEnhancedActionKeyMapping& W = Ctx->MapKey(MoveAction, EKeys::W);
		W.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Ctx));
		FEnhancedActionKeyMapping& S = Ctx->MapKey(MoveAction, EKeys::S);
		S.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Ctx));
		S.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
		Ctx->MapKey(MoveAction, EKeys::D);
		FEnhancedActionKeyMapping& A = Ctx->MapKey(MoveAction, EKeys::A);
		A.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
		Ctx->MapKey(MoveAction, EKeys::Gamepad_Left2D);
	}
	// Mouse look (invert Y so pushing the mouse forward looks up)
	{
		FEnhancedActionKeyMapping& M = Ctx->MapKey(LookAction, EKeys::Mouse2D);
		UInputModifierNegate* Neg = NewObject<UInputModifierNegate>(Ctx);
		Neg->bX = false;
		Neg->bY = true;
		Neg->bZ = false;
		M.Modifiers.Add(Neg);
		FEnhancedActionKeyMapping& G = Ctx->MapKey(LookAction, EKeys::Gamepad_Right2D);
		UInputModifierNegate* NegG = NewObject<UInputModifierNegate>(Ctx);
		NegG->bX = false;
		NegG->bY = true;
		NegG->bZ = false;
		G.Modifiers.Add(NegG);
	}
	Ctx->MapKey(JumpAction, EKeys::SpaceBar);
	Ctx->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	Ctx->MapKey(SprintAction, EKeys::LeftShift);
	Ctx->MapKey(SprintAction, EKeys::Gamepad_LeftThumbstick);
	Ctx->MapKey(WalkAction, EKeys::CapsLock);
	Ctx->MapKey(ViewAction, EKeys::V);
	Ctx->MapKey(ViewAction, EKeys::Gamepad_RightThumbstick);
	Ctx->MapKey(ZoomAction, EKeys::MouseWheelAxis);
	Ctx->MapKey(CrouchAction, EKeys::C);

	// sprite body tests (docs/sprites.md): attack, emotes, next look
	AttackAction = MakeAction(TEXT("IA_Attack"), EInputActionValueType::Boolean);
	NextLookAction = MakeAction(TEXT("IA_NextLook"), EInputActionValueType::Boolean);
	PhotoAction = MakeAction(TEXT("IA_Photo"), EInputActionValueType::Boolean);
	Ctx->MapKey(AttackAction, EKeys::LeftMouseButton);
	Ctx->MapKey(NextLookAction, EKeys::L);
	Ctx->MapKey(PhotoAction, EKeys::P);
	const FKey EmoteKeyBindings[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four};
	for (int32 i = 0; i < UE_ARRAY_COUNT(EmoteKeys); ++i)
	{
		UInputAction* A = MakeAction(*FString::Printf(TEXT("IA_Emote_%s"), *EmoteKeys[i].ToString()), EInputActionValueType::Boolean);
		Ctx->MapKey(A, EmoteKeyBindings[i]);
		EmoteActions.Add(A);
	}
}

void AMRCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();
	ApplyCommandLineAppearance();  // this machine's own character

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultContext, 0);
		}
	}

	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMRCharacter::OnMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMRCharacter::OnLook);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &AMRCharacter::OnSprintStarted);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &AMRCharacter::OnSprintStopped);
	Input->BindAction(WalkAction, ETriggerEvent::Started, this, &AMRCharacter::OnToggleWalk);
	Input->BindAction(ViewAction, ETriggerEvent::Started, this, &AMRCharacter::OnToggleView);
	Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AMRCharacter::OnZoom);
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &AMRCharacter::OnCrouchToggle);
	Input->BindAction(AttackAction, ETriggerEvent::Started, this, &AMRCharacter::OnAttack);
	Input->BindAction(NextLookAction, ETriggerEvent::Started, this, &AMRCharacter::OnNextLook);
	Input->BindAction(PhotoAction, ETriggerEvent::Started, this, &AMRCharacter::OnPhoto);
	for (int32 i = 0; i < EmoteActions.Num(); ++i)
	{
		Input->BindAction(EmoteActions[i], ETriggerEvent::Started, this, &AMRCharacter::OnEmote, EmoteKeys[i]);
	}
}

void AMRCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (!Controller)
	{
		return;
	}
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
}

void AMRCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	if (!IsFirstPerson())
	{
		// the third-person cameras stay near eye level: a limited tilt (mr.Camera.ThirdPersonPitch)
		const float Limit = ThirdPersonPitchLimit();
		if (ViewMode == EMRViewMode::Behind || ViewMode == EMRViewMode::Front)
		{
			// the fixed views tilt their own camera (in front, mouse up looks down at the character)
			FixedViewPitch = FMath::Clamp(FixedViewPitch + Axis.Y * (ViewMode == EMRViewMode::Front ? -1.f : 1.f), -Limit, Limit);
			ApplyViewMode();
			return;
		}
		AddControllerPitchInput(Axis.Y);
		ClampThirdPersonPitch();
		return;
	}
	AddControllerPitchInput(Axis.Y);
}

void AMRCharacter::ClampThirdPersonPitch()
{
	if (!Controller)
	{
		return;
	}
	FRotator Rot = Controller->GetControlRotation();
	const float Limit = ThirdPersonPitchLimit();
	const float Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rot.Pitch), -Limit, Limit);
	if (!FMath::IsNearlyEqual(Pitch, FRotator::NormalizeAxis(Rot.Pitch)))
	{
		Rot.Pitch = Pitch;
		Controller->SetControlRotation(Rot);
	}
}

void AMRCharacter::OnSprintStarted()
{
	if (UMRCharacterMovementComponent* Move = GetMRMovement())
	{
		Move->SetWantsToSprint(true);
	}
}

void AMRCharacter::OnSprintStopped()
{
	if (UMRCharacterMovementComponent* Move = GetMRMovement())
	{
		Move->SetWantsToSprint(false);
	}
}

void AMRCharacter::OnToggleWalk()
{
	bWalkToggled = !bWalkToggled;
	if (UMRCharacterMovementComponent* Move = GetMRMovement())
	{
		Move->SetWantsToWalk(bWalkToggled);
	}
}

void AMRCharacter::OnToggleView()
{
	SetViewMode(static_cast<EMRViewMode>((static_cast<uint8>(ViewMode) + 1) % 4));
}

void AMRCharacter::OnZoom(const FInputActionValue& Value)
{
	const float Wheel = Value.Get<float>();
	if (FMath::IsNearlyZero(Wheel) || !CameraBoom)
	{
		return;
	}
	if (IsFirstPerson())
	{
		if (Wheel < 0.f) // scroll out of first person
		{
			SetFirstPerson(false);
		}
		return;
	}
	if (ViewMode != EMRViewMode::Chase)
	{
		FixedViewArm = FMath::Clamp(FixedViewArm - Wheel * 40.f, 100.f, MaxArmLength);
		ApplyViewMode();
		return;
	}
	const float NewLength = CameraBoom->TargetArmLength - Wheel * 40.f;
	if (NewLength < 80.f) // scrolled all the way in
	{
		SetFirstPerson(true);
		return;
	}
	CameraBoom->TargetArmLength = FMath::Min(NewLength, MaxArmLength);
}

void AMRCharacter::OnCrouchToggle()
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

// ---------------------------------------------------------------- sprite body tests

void AMRCharacter::OnAttack()
{
	const FMRSpriteLook* Look = FMRSpriteLibrary::Get().Looks.Find(SpriteAppearance.Look);
	PlaySpriteAction(Look && Look->Find(TEXT("weapon")) ? TEXT("weapon_attack") : TEXT("fist_attack"));
}

void AMRCharacter::OnEmote(FName Action)
{
	const FName Current = SpriteBody ? SpriteBody->GetAction() : SpriteAction.Action;
	PlaySpriteAction(Current == Action ? NAME_None : Action);  // the same key again stops (the dance)
}

void AMRCharacter::OnNextLook()
{
	TArray<FName> Names;
	FMRSpriteLibrary::Get().Looks.GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	if (Names.Num() > 0)
	{
		FMRSpriteAppearance A = SpriteAppearance;
		A.Look = Names[(Names.IndexOfByKey(A.Look) + 1) % Names.Num()];
		A.Skin = A.Hair = A.Shirt = A.Pants = -1;  // the look's own colours
		SetSpriteAppearance(A);
	}
}

void AMRCharacter::OnPhoto()
{
	TakePhoto();
}

void AMRCharacter::TakePhoto()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !IsLocallyControlled())
	{
		return;
	}
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Photos"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = FPaths::Combine(Dir, FString::Printf(TEXT("photo_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	// no HUD (first-person hands) in the picture; it comes back a moment later
	AHUD* Hud = PC->GetHUD();
	if (Hud)
	{
		Hud->bShowHUD = false;
	}
	GetHighResScreenshotConfig().FilenameOverride = File;
	PC->ConsoleCommand(TEXT("HighResShot 2"));
	UE_LOG(LogMeridian, Display, TEXT("Photo: %s"), *File);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [PC]()
	{
		if (IsValid(PC) && PC->GetHUD())
		{
			PC->GetHUD()->bShowHUD = true;
		}
	}), 0.3f, false);
}

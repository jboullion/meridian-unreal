#include "Character/MRCharacter.h"

#include "AbilitySystemComponent.h"
#include "Abilities/MRAttributeSet.h"
#include "Camera/CameraComponent.h"
#include "Character/MRCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Net/UnrealNetwork.h"
#include "Player/MRPlayerState.h"
#include "UObject/ConstructorHelpers.h"
#include "Zones/MRZoneSubsystem.h"

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
	ApplyViewMode();
}

void AMRCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMRCharacter, bFirstPerson);
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
	if (bFirstPerson == bNewFirstPerson)
	{
		return;
	}
	bFirstPerson = bNewFirstPerson;
	ApplyViewMode(); // predict locally
	if (!HasAuthority())
	{
		ServerSetFirstPerson(bNewFirstPerson);
	}
}

void AMRCharacter::ServerSetFirstPerson_Implementation(bool bNewFirstPerson)
{
	bFirstPerson = bNewFirstPerson;
	ApplyViewMode();
}

void AMRCharacter::OnRep_FirstPerson()
{
	ApplyViewMode();
}

void AMRCharacter::ApplyViewMode()
{
	// Rotation rules must match on client and server or movement will be corrected.
	bUseControllerRotationYaw = bFirstPerson;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = !bFirstPerson;
		Move->RotationRate = FRotator(0.f, 540.f, 0.f);
	}
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = bFirstPerson ? 0.f : ThirdPersonArmLength;
		CameraBoom->SocketOffset = bFirstPerson ? FVector::ZeroVector : FVector(0.f, 45.f, 25.f);
	}
	if (PlaceholderBody)
	{
		PlaceholderBody->SetOwnerNoSee(bFirstPerson);
	}
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
}

void AMRCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();

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
	AddControllerPitchInput(Axis.Y);
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
	SetFirstPerson(!bFirstPerson);
}

void AMRCharacter::OnZoom(const FInputActionValue& Value)
{
	const float Wheel = Value.Get<float>();
	if (FMath::IsNearlyZero(Wheel) || !CameraBoom)
	{
		return;
	}
	if (bFirstPerson)
	{
		if (Wheel < 0.f) // scroll out of first person
		{
			SetFirstPerson(false);
		}
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

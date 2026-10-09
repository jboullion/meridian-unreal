#include "Character/MRCharacter.h"

#include "AbilitySystemComponent.h"
#include "Abilities/MRAttributeSet.h"
#include "Camera/CameraComponent.h"
#include "Character/MRCharacterMovementComponent.h"
#include "Character/MRSpriteBodyComponent.h"
#include "Character/MRSpriteData.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
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
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRProtocol.h"
#include "Core/MRSettings.h"
#include "Zones/MRZoneSubsystem.h"
#include "HAL/FileManager.h"
#include "HighResScreenshot.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Player/MRPlayerState.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	TAutoConsoleVariable<float> CVarFieldOfView(TEXT("mr.Camera.FOV"), 90.f, TEXT("The camera's horizontal field of view, degrees (Options > Graphics)."));
	TAutoConsoleVariable<float> CVarThirdPersonPitch(TEXT("mr.Camera.ThirdPersonPitch"), 20.f,
		TEXT("How far (degrees) the third-person cameras may tilt up or down from eye level (0 = locked at eye level)."));

	float ThirdPersonPitchLimit() { return FMath::Clamp(CVarThirdPersonPitch.GetValueOnGameThread(), 0.f, 89.f); }

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

	// Drawn as a sprite (UMRSpriteBodyComponent, created at BeginPlay): ACharacter's skeletal mesh is unused.
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetVisibility(false);

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

	if (HasAuthority() && SpriteAppearance.Look.IsNone())
	{
		SpriteAppearance.Look = DefaultSpriteLook;  // replicated: every client draws this look
	}
	CreateSpriteBody(SpriteAppearance.Look.IsNone() ? DefaultSpriteLook : SpriteAppearance.Look);
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
	// face parts: only bgfs the game has converted ("blank" = bald)
	for (FName* Part : {&A.HeadBgf, &A.HairBgf, &A.EyesBgf, &A.NoseBgf, &A.MouthBgf, &A.BodyBgf, &A.LeftArmBgf, &A.RightArmBgf, &A.LegsBgf})
	{
		const FString Bgf = Part->ToString().ToLower();
		if (!Part->IsNone() && Bgf != TEXT("blank") && !FMRSpriteLibrary::Get().Atlases.Contains(Bgf))
		{
			*Part = NAME_None;
		}
	}
	// equipment: only converted overlays, a few of them
	A.Overlays.RemoveAll([](const FMRSpriteOverlay& O) { return !FMRSpriteLibrary::Get().Equipment.Contains(O.Bgf.ToString().ToLower()); });
	A.Overlays.SetNum(FMath::Min(A.Overlays.Num(), 6));
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
		SpriteBody->SetAppearance(A);
	}
	ApplySpriteHeight(A.HeightPct / 100.f);
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
	// equipment to try offline (docs/adr/0012 M2b): -MRSpriteEquip=bte,swordov@22:4,metlshld@32:2,helm@13,nohair
	// (a bgf alone swaps the torso, arms or legs it is for; bgf@hotspot[:group] adds an overlay)
	if (FParse::Value(FCommandLine::Get(), TEXT("MRSpriteEquip="), Value, false))
	{
		TArray<FString> Items;
		Value.ParseIntoArray(Items, TEXT(","));
		for (const FString& Item : Items)
		{
			FString Bgf = Item, Rest;
			if (Item == TEXT("nohair"))
			{
				A.HairBgf = TEXT("blank");
			}
			else if (Item.Split(TEXT("@"), &Bgf, &Rest))
			{
				FString Hs = Rest, Group = TEXT("1");
				Rest.Split(TEXT(":"), &Hs, &Group);
				FMRSpriteOverlay O;
				O.Bgf = FName(*Bgf.ToLower());
				O.Hotspot = static_cast<uint8>(FCString::Atoi(*Hs));
				O.Group = FCString::Atoi(*Group);
				A.Overlays.Add(O);
			}
			else if (const FMRSpriteEquipment* E = FMRSpriteLibrary::Get().Equipment.Find(Bgf.ToLower()))
			{
				FName* Slot = E->Kind == TEXT("body") ? &A.BodyBgf : E->Kind == TEXT("left_arm") ? &A.LeftArmBgf
					: E->Kind == TEXT("right_arm") ? &A.RightArmBgf : E->Kind == TEXT("legs") ? &A.LegsBgf : nullptr;
				if (Slot)
				{
					*Slot = FName(*Bgf.ToLower());
					if (E->Kind == TEXT("body"))
					{
						A.BodyXlat = 0;  // armour in its own colours
					}
				}
			}
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
	const bool bAttack = Action == TEXT("fist_attack") || Action == TEXT("weapon_attack");
	if (bAttack)
	{
		// the original's 1 s between attacks; a little slack for the owner's clock and the network
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - ServerLastAttackTime < AttackIntervalSeconds * 0.8)
		{
			return;
		}
		ServerLastAttackTime = Now;
	}
	SpriteAction.Action = Action;
	SpriteAction.Seq++;
	SpriteAction.ServerTime = GetWorld()->GetTimeSeconds();
	if (bAttack)
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

void AMRCharacter::CreateSpriteBody(FName Look)
{
	AppearanceDescription = FString::Printf(TEXT("sprite:%s"), *Look.ToString());
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

void AMRCharacter::ApplyFirstPersonVisibility()
{
	// Only the player's own view changes: everyone else always sees the whole character.
	// In first person the owner sees the original's 2D hands instead (AMRHUD).
	if (SpriteBody)
	{
		SpriteBody->SetOwnerNoSee(IsFirstPerson());  // the shadow card still casts
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
	if (IsLocallyControlled() && Camera)
	{
		// Options > Graphics: the field of view
		const float Fov = FMath::Clamp(CVarFieldOfView.GetValueOnGameThread(), 60.f, 120.f);
		if (!FMath::IsNearlyEqual(Camera->FieldOfView, Fov))
		{
			Camera->SetFieldOfView(Fov);
		}
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
	const float Delta = VigorRegenPerSecond * DeltaSeconds;
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
	TurnAction = MakeAction(TEXT("IA_Turn"), EInputActionValueType::Axis1D);
	GoAction = MakeAction(TEXT("IA_Go"), EInputActionValueType::Boolean);
	WalkAction = MakeAction(TEXT("IA_Walk"), EInputActionValueType::Boolean);
	ViewAction = MakeAction(TEXT("IA_ToggleView"), EInputActionValueType::Boolean);
	ZoomAction = MakeAction(TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	// sprite actions (docs/sprites.md): attack, emotes, next look
	AttackAction = MakeAction(TEXT("IA_Attack"), EInputActionValueType::Boolean);
	NextLookAction = MakeAction(TEXT("IA_NextLook"), EInputActionValueType::Boolean);
	PhotoAction = MakeAction(TEXT("IA_Photo"), EInputActionValueType::Boolean);
	for (int32 i = 0; i < UE_ARRAY_COUNT(EmoteKeys); ++i)
	{
		EmoteActions.Add(MakeAction(*FString::Printf(TEXT("IA_Emote_%s"), *EmoteKeys[i].ToString()), EInputActionValueType::Boolean));
	}
	DefaultContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Default"));
	MapKeys();
}

void AMRCharacter::MapKeys()
{
	// the player's keys (MRKeys: Options > Controls), and the gamepad's, which stay
	UInputMappingContext* Ctx = DefaultContext;
	Ctx->UnmapAll();
	MappedKeyVersion = MRKeys::Version();
	auto Map = [Ctx](UInputAction* Action, FName Binding) -> FEnhancedActionKeyMapping*
	{
		const FKey Key = MRKeys::Get(Binding);
		return Key.IsValid() ? &Ctx->MapKey(Action, Key) : nullptr;
	};
	// move -> Axis2D (X = right, Y = forward)
	if (FEnhancedActionKeyMapping* W = Map(MoveAction, TEXT("MoveForward")))
	{
		W->Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Ctx));
	}
	if (FEnhancedActionKeyMapping* S = Map(MoveAction, TEXT("MoveBack")))
	{
		S->Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Ctx));
		S->Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
	}
	Map(MoveAction, TEXT("StrafeRight"));
	if (FEnhancedActionKeyMapping* A = Map(MoveAction, TEXT("StrafeLeft")))
	{
		A->Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
	}
	Ctx->MapKey(MoveAction, EKeys::Gamepad_Left2D);
	// turning by keys (the original's arrows)
	Map(TurnAction, TEXT("TurnRight"));
	if (FEnhancedActionKeyMapping* L = Map(TurnAction, TEXT("TurnLeft")))
	{
		L->Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
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
	Map(GoAction, TEXT("Go"));
	Ctx->MapKey(GoAction, EKeys::Gamepad_FaceButton_Bottom);
	Map(WalkAction, TEXT("Walk"));
	Ctx->MapKey(WalkAction, EKeys::Gamepad_LeftThumbstick);
	Map(ViewAction, TEXT("View"));
	Ctx->MapKey(ViewAction, EKeys::Gamepad_RightThumbstick);
	Ctx->MapKey(ZoomAction, EKeys::MouseWheelAxis);
	Map(AttackAction, TEXT("Attack"));
	Ctx->MapKey(AttackAction, EKeys::Gamepad_RightTrigger);
	// offline test keys: the next look, a photo, emotes on function keys (online the function keys
	// are the quick chat's: AMRPlayerController)
	Ctx->MapKey(NextLookAction, EKeys::L);
	Ctx->MapKey(PhotoAction, EKeys::P);
	const FKey EmoteKeyBindings[] = {EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F9};
	for (int32 i = 0; i < EmoteActions.Num() && i < UE_ARRAY_COUNT(EmoteKeyBindings); ++i)
	{
		Ctx->MapKey(EmoteActions[i], EmoteKeyBindings[i]);
	}
}

void AMRCharacter::RemapKeysIfChanged()
{
	if (!DefaultContext || MappedKeyVersion == MRKeys::Version())
	{
		return;
	}
	MapKeys();
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->RequestRebuildControlMappings();
		}
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
	Input->BindAction(TurnAction, ETriggerEvent::Triggered, this, &AMRCharacter::OnTurn);
	Input->BindAction(GoAction, ETriggerEvent::Started, this, &AMRCharacter::OnGo);
	Input->BindAction(WalkAction, ETriggerEvent::Started, this, &AMRCharacter::OnWalkStarted);
	Input->BindAction(WalkAction, ETriggerEvent::Completed, this, &AMRCharacter::OnWalkStopped);
	Input->BindAction(ViewAction, ETriggerEvent::Started, this, &AMRCharacter::OnToggleView);
	Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AMRCharacter::OnZoom);
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

void AMRCharacter::OnTurn(const FInputActionValue& Value)
{
	// the original's arrow keys: a steady turn (degrees a second)
	constexpr float TurnDegreesPerSecond = 150.f;
	if (Controller && GetWorld())
	{
		FRotator Rot = Controller->GetControlRotation();
		Rot.Yaw += Value.Get<float>() * TurnDegreesPerSecond * GetWorld()->GetDeltaSeconds();
		Controller->SetControlRotation(Rot);
	}
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

void AMRCharacter::OnWalkStarted()
{
	if (UMRCharacterMovementComponent* Move = GetMRMovement())
	{
		Move->SetWantsToWalk(true);
	}
}

void AMRCharacter::OnWalkStopped()
{
	if (UMRCharacterMovementComponent* Move = GetMRMovement())
	{
		Move->SetWantsToWalk(false);
	}
}

void AMRCharacter::OnGo()
{
	// online the server decides whether we stand on a door (BP_REQ_GO), as the original's space bar
	if (UMRNetWorldSubsystem* NetWorld = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>(); NetWorld && NetWorld->IsActive())
	{
		NetWorld->RequestGo();
		return;
	}
	ServerGo();
}

void AMRCharacter::ServerGo_Implementation()
{
	if (UMRZoneSubsystem* Zones = GetWorld()->GetSubsystem<UMRZoneSubsystem>())
	{
		Zones->TryGo(this);
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
	// the wheel alone selects the hotbar slot (AMRPlayerController); Ctrl + wheel zooms
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !(PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)))
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

// ---------------------------------------------------------------- sprite actions

void AMRCharacter::OnAttack()
{
	// online the server decides: the attack goes up, the swing comes back (docs/adr/0012 M4)
	if (UMRNetWorldSubsystem* NetWorld = GetWorld()->GetSubsystem<UMRNetWorldSubsystem>(); NetWorld && NetWorld->IsActive())
	{
		NetWorld->Attack();
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastAttackTime < AttackIntervalSeconds)
	{
		return;  // the original allowed one attack a second
	}
	LastAttackTime = Now;
	const FMRSpriteLook* Look = FMRSpriteLibrary::Get().Looks.Find(SpriteAppearance.Look);
	PlaySpriteAction(Look && Look->Find(TEXT("weapon")) ? TEXT("weapon_attack") : TEXT("fist_attack"));
}

void AMRCharacter::SetViewEffects(const FVector& EyeOffset, float Roll, float BlurPixels, bool bInvert)
{
	if (!Camera)
	{
		return;
	}
	Camera->SetRelativeLocationAndRotation(EyeOffset, FRotator(0.f, 0.f, Roll));
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	// blur: everything past a hand's breadth out of focus, more as the original's blur swells
	const bool bBlur = BlurPixels > 0.f;
	PP.bOverride_DepthOfFieldFocalDistance = bBlur;
	PP.bOverride_DepthOfFieldFstop = bBlur;
	PP.bOverride_DepthOfFieldMinFstop = bBlur;
	PP.DepthOfFieldFocalDistance = 10.f;
	PP.DepthOfFieldFstop = FMath::Lerp(16.f, 4.f, FMath::Clamp(BlurPixels / 6.f, 0.f, 1.f));
	PP.DepthOfFieldMinFstop = 0.f;
	// invert: the grading comes before the tonemapper, in linear light, so mirror it about mid grey
	// (0.18: 0.36 - colour) rather than 1 - colour, which tonemaps to near white
	PP.bOverride_ColorGain = bInvert;
	PP.bOverride_ColorOffset = bInvert;
	PP.ColorGain = FVector4(-1.0, -1.0, -1.0, 1.0);
	PP.ColorOffset = FVector4(0.36, 0.36, 0.36, 0.0);
}

bool AMRCharacter::IsOnline() const
{
	const UMRNetWorldSubsystem* NetWorld = GetWorld() ? GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	return NetWorld && NetWorld->IsActive();
}

void AMRCharacter::OnEmote(FName Action)
{
	if (IsOnline())
	{
		return;  // online the function keys run the quick chat (AMRPlayerController::OnQuickChatKey)
	}
	const FName Current = SpriteBody ? SpriteBody->GetAction() : SpriteAction.Action;
	PlaySpriteAction(Current == Action ? NAME_None : Action);  // the same key again stops (the dance)
}

void AMRCharacter::OnNextLook()
{
	if (IsOnline())
	{
		return;  // the server decides how we look (and L is the mail key online)
	}
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

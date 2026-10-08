#include "Character/MRSpriteBodyComponent.h"

#include "CanvasItem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeridianRemastered.h"
#include "UObject/ConstructorHelpers.h"
#include "Character/MRCharacterMovementComponent.h"

namespace
{
	TAutoConsoleVariable<float> CVarSpriteTexels(TEXT("mr.Sprite.TexelsPerCm"), 4.65f,
		TEXT("Render target texels per cm of sprite (4.65 = 4 per original player torso pixel, as its atlas holds); capped at 1024 texels a side."));
	TAutoConsoleVariable<float> CVarSpriteBillboard(TEXT("mr.Sprite.Billboard"), 1.f,
		TEXT("How much of the camera's pitch the quad follows, pivoting at the feet: 1 = always parallel to the screen ")
		TEXT("(never distorted, as the original looked: its pitch was limited to about 11 degrees), 0 = always upright."));
	TAutoConsoleVariable<int32> CVarSpriteShadow(TEXT("mr.Sprite.Shadow"), 1,
		TEXT("0: no shadow; 1: an upright card turned to the sun casts the sprite's silhouette."));
	TAutoConsoleVariable<int32> CVarSpriteUnlit(TEXT("mr.Sprite.Unlit"), 0,
		TEXT("1: unlit sprites (M_SpriteBodyUnlit), closest to the original; 0: lit by the world."));
	TAutoConsoleVariable<float> CVarSpriteWalkRef(TEXT("mr.Sprite.WalkRefSpeed"), -1.f,
		TEXT("Ground speed (cm/s) at which the walk cycle plays at the original rate; -1 = the player's run speed; 0 = always the ")
		TEXT("original rate (100 ms a leg pose), as the original client did whatever the speed."));
	TAutoConsoleVariable<int32> CVarSmoothTweens(TEXT("mr.Sprite.Smooth.Tweens"), 1,
		TEXT("Show the in-between frames (tools/sprites/tweens.py) between the original poses."));
	TAutoConsoleVariable<float> CVarSmoothOnceWindow(TEXT("mr.Sprite.Smooth.OnceWindow"), 0.35f,
		TEXT("One-shot actions (attacks, wave, cast): the in-betweens play in this last fraction of each pose, which holds ")
		TEXT("still before it, as the original's poses did (1 = in-betweens across the whole pose, as walking does)."));
	TAutoConsoleVariable<int32> CVarBackArmsUnder(TEXT("mr.Sprite.BackArmsUnder"), 1,
		TEXT("1: seen from behind, the arms (and what they hold) are drawn under the torso. 0: as the original's bitmaps ")
		TEXT("say, which puts a punch or a backswing on top of the player's back."));
	TAutoConsoleVariable<float> CVarSmoothCrossfade(TEXT("mr.Sprite.Smooth.Crossfade"), 0.f,
		TEXT("Crossfade each part into its next frame over this last fraction of every frame (0 off, 1 the whole frame)."));
	TAutoConsoleVariable<float> CVarSmoothAngleFade(TEXT("mr.Sprite.Smooth.AngleFade"), 0.f,
		TEXT("Seconds to fade from one view to the next when the viewing angle changes (0 off)."));
	TAutoConsoleVariable<float> CVarSmoothMotion(TEXT("mr.Sprite.Smooth.Motion"), 0.f,
		TEXT("Procedural motion of the whole sprite: walk bob, idle breathing, leaning into turns, landing squash (0 off)."));
	TAutoConsoleVariable<float> CVarSpriteAlbedo(TEXT("mr.Sprite.Albedo"), 0.64f,
		TEXT("Lit sprites: colour multiplier (sunlit sprites read too bright at 1; 0.75 until 2026-10-07, then 15 % less)."));
	TAutoConsoleVariable<float> CVarSpriteNormalUp(TEXT("mr.Sprite.NormalUp"), 0.4f,
		TEXT("Lit sprites: how much the shading normal points up (1 = up: lit like the ground)."));
	TAutoConsoleVariable<float> CVarSpriteSunFace(TEXT("mr.Sprite.SunFace"), 0.6f,
		TEXT("Lit sprites: how much the shading normal faces the sun's side rather than the camera (1 = brightness never changes as the camera orbits)."));
	TAutoConsoleVariable<float> CVarSpriteAmbient(TEXT("mr.Sprite.Ambient"), 0.51f,
		TEXT("Lit sprites indoors: share of the zone's ambient floor (MPC_Environment.SectorAmbient) added, as the walls get it ")
		TEXT("(0.6 until 2026-10-07, then 15 % less)."));
	TAutoConsoleVariable<FString> CVarSpriteUV(TEXT("mr.Sprite.UV"), TEXT("1 0 1"),
		TEXT("Quad UV mapping: 'SwapUV FlipU FlipV' (0/1 each)."));

	const TCHAR* PlaneMesh = TEXT("/Engine/BasicShapes/Plane.Plane");
	const FName WalkName(TEXT("walk"));
	const FName StandName(TEXT("stand"));
	const FName RightArm(TEXT("right_arm"));
	const FName WeaponName(TEXT("weapon"));
	const FName EyesName(TEXT("eyes"));
	const FName MouthName(TEXT("mouth"));
	constexpr float MovingSpeed = 20.f;
}

UMRSpriteBodyComponent::UMRSpriteBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;  // after movement and the camera
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCanEverAffectNavigation(false);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	bAffectDistanceFieldLighting = false;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(PlaneMesh);
	if (Plane.Succeeded())
	{
		SetStaticMesh(Plane.Object);
	}
}

void UMRSpriteBodyComponent::OnRegister()
{
	Super::OnRegister();
	if (!ShadowCard && GetOwner())
	{
		ShadowCard = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("SpriteShadowCard"));
		ShadowCard->SetStaticMesh(GetStaticMesh());
		ShadowCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShadowCard->SetUsingAbsoluteLocation(true);
		ShadowCard->SetUsingAbsoluteRotation(true);
		ShadowCard->SetUsingAbsoluteScale(true);
		ShadowCard->SetHiddenInGame(true);
		ShadowCard->bCastHiddenShadow = true;
		ShadowCard->SetCastShadow(true);
		ShadowCard->bAffectDistanceFieldLighting = false;
		ShadowCard->SetBoundsScale(3.f);  // its shadow-pass vertices move up to MaxPush along the light
		ShadowCard->SetupAttachment(this);
		ShadowCard->RegisterComponent();
	}
}

void UMRSpriteBodyComponent::OnUnregister()
{
	if (ShadowCard)
	{
		ShadowCard->DestroyComponent();
		ShadowCard = nullptr;
	}
	Super::OnUnregister();
}

// ------------------------------------------------------------------------------ setup

void UMRSpriteBodyComponent::SetLook(FName LookName)
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	Look = Lib.Looks.Find(LookName);
	if (!Look)
	{
		UE_LOG(LogMeridian, Warning, TEXT("Sprite look %s not in player_parts.json"), *LookName.ToString());
		if (Lib.Looks.Num() > 0)
		{
			auto It = Lib.Looks.CreateConstIterator();
			Look = &It->Value;
		}
	}
	LastDrawKey = 0;
	Target = nullptr;
	if (Look)
	{
		EnsureTarget();
		ApplyMaterial();
		SetBaseAction(StandName);
		ActionTracks.Reset();
		CurrentAction = NAME_None;
		PartXlat.Reset();
		for (const FMRSpritePart& Part : Look->Parts)
		{
			PartXlat.Add(Part.Name, Part.Xlat);
		}
		const FMRSpritePart* Weapon = Look->Find(WeaponName);
		FirstPersonDef = FMRSpriteLibrary::Get().FirstPerson.Find(Weapon ? Weapon->Bgf : FString(TEXT("fist")));
		FirstPersonTrack = FMRSpriteTrack();
		FirstPersonTrack.Group = FirstPersonDef ? FirstPersonDef->Hold : 0;
	}
}

bool UMRSpriteBodyComponent::GetFirstPersonFrame(UTexture2D*& OutTexture, FBox2f& OutUV, FIntPoint& OutSize, FIntPoint& OutOffset)
{
	if (!FirstPersonDef || FirstPersonTrack.Group <= 0)
	{
		return false;
	}
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	const FMRSpriteBgf* Bgf = Lib.FindBgf(FirstPersonDef->Bgf);
	const FString Key = FirstPersonDef->Bgf;
	const FMRSpriteAtlas* Atlas = Lib.Atlases.Find(Key);
	const int32 Bitmap = Bgf ? Bgf->BitmapIndex(FirstPersonTrack.Group - 1, 0) : INDEX_NONE;
	const FIntRect* Cell = Atlas && Bitmap != INDEX_NONE ? Atlas->Cells.Find(Bitmap) : nullptr;
	OutTexture = Cell ? AtlasTexture(Key) : nullptr;
	if (!OutTexture)
	{
		return false;
	}
	const FVector2f AtlasSize(Atlas->Size.X, Atlas->Size.Y);
	OutUV = FBox2f(FVector2f(Cell->Min.X, Cell->Min.Y) / AtlasSize, FVector2f(Cell->Max.X, Cell->Max.Y) / AtlasSize);
	const FMRSpriteBitmap& Bm = Bgf->Bitmaps[Bitmap];
	OutSize = FIntPoint(Bm.W, Bm.H);
	OutOffset = FIntPoint(Bm.XOff, Bm.YOff);
	return true;
}

void UMRSpriteBodyComponent::SetColours(int32 Skin, int32 Hair, int32 Shirt, int32 Pants)
{
	if (!Look)
	{
		return;
	}
	// the look's own colours where none is given: its shirt and pants from their translations
	const FMRSpritePart* Body = Look->Find(TEXT("body"));
	const FMRSpritePart* Legs = Look->Find(TEXT("legs"));
	const FMRSpritePart* Head = Look->Find(TEXT("head"));
	const int32 LookSkin = Head ? FMath::Clamp(Head->Xlat - 1, 0, FMRSpriteColours::NumSkins - 1) : 2;
	const int32 S = Skin >= 0 ? Skin : LookSkin;
	const int32 ShirtC = Shirt >= 0 ? Shirt : (Body ? FMRSpriteColours::ClothesOf(Body->Xlat) : INDEX_NONE);
	const int32 PantsC = Pants >= 0 ? Pants : (Legs ? FMRSpriteColours::ClothesOf(Legs->Xlat) : INDEX_NONE);
	for (const FMRSpritePart& Part : Look->Parts)
	{
		int32 X = Part.Xlat;
		const FString Role = Part.Name.ToString();
		if (Role == TEXT("head") || Role == TEXT("eyes") || Role == TEXT("mouth") || Role == TEXT("nose"))
		{
			X = Skin >= 0 ? FMRSpriteColours::SkinXlat(S) : X;
		}
		else if (Role == TEXT("hair"))
		{
			X = Hair >= 0 ? FMRSpriteColours::HairXlat(Hair) : X;
		}
		else if (Role == TEXT("body") || Role == TEXT("left_arm") || Role == TEXT("right_arm"))
		{
			X = ShirtC != INDEX_NONE && (Skin >= 0 || Shirt >= 0) ? FMRSpriteColours::ClothesXlat(ShirtC, S) : X;
		}
		else if (Role == TEXT("legs"))
		{
			X = PantsC != INDEX_NONE && (Skin >= 0 || Pants >= 0) ? FMRSpriteColours::ClothesXlat(PantsC, S) : X;
		}
		PartXlat.Add(Part.Name, X);
	}
	LastDrawKey = 0;  // redraw the code target
}

void UMRSpriteBodyComponent::ApplyLightingParams()
{
	if (!Material)
	{
		return;
	}
	const float Albedo = CVarSpriteAlbedo.GetValueOnGameThread();
	const float NormalUp = CVarSpriteNormalUp.GetValueOnGameThread();
	const float Ambient = CVarSpriteAmbient.GetValueOnGameThread();
	Material->SetScalarParameterValue(TEXT("SunFace"), CVarSpriteSunFace.GetValueOnGameThread());
	if (Albedo != LastAlbedo || NormalUp != LastNormalUp || Ambient != LastAmbient)
	{
		Material->SetScalarParameterValue(TEXT("Albedo"), Albedo);
		Material->SetScalarParameterValue(TEXT("NormalUp"), NormalUp);
		Material->SetScalarParameterValue(TEXT("SpriteAmbient"), Ambient);
		LastAlbedo = Albedo;
		LastNormalUp = NormalUp;
		LastAmbient = Ambient;
	}
}

bool UMRSpriteBodyComponent::WantsUnlit() const
{
	return bForceUnlit || CVarSpriteUnlit.GetValueOnGameThread() != 0;
}

void UMRSpriteBodyComponent::SetForceUnlit(bool bInForceUnlit)
{
	bForceUnlit = bInForceUnlit;
	if (Material && bUnlit != WantsUnlit())
	{
		ApplyMaterial();
	}
}

void UMRSpriteBodyComponent::SetHeightScale(float InScale)
{
	HeightScale = FMath::Clamp(InScale, 0.5f, 1.5f);
}

float UMRSpriteBodyComponent::GetStandingHeightCm() const
{
	if (!Look)
	{
		return 0.f;
	}
	return -Look->Bounds.Min.Y * FMRSpriteLibrary::Get().CmPerBasePixel(LastShrink) * HeightScale;
}

void UMRSpriteBodyComponent::EnsureTarget()
{
	if (Target || !Look)
	{
		return;
	}
	const FVector2f Size = Look->Bounds.GetSize();
	// the look's body shrink sets its pixel size (a bunny's 330 pixels are 0.6 m, a player's 214 are 1.84 m)
	const FMRSpritePart* Body = Look->Find(TEXT("body"));
	const FMRSpriteBgf* BodyBgf = Body ? FMRSpriteLibrary::Get().FindBgf(Body->Bgf) : nullptr;
	LastShrink = BodyBgf ? BodyBgf->Shrink : 4;
	const float CmPerPx = FMRSpriteLibrary::Get().CmPerBasePixel(LastShrink);
	TexelsPerBasePixel = FMath::Min(CVarSpriteTexels.GetValueOnGameThread() * CmPerPx, 1024.f / FMath::Max(Size.X, Size.Y));
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Target->ClearColor = FLinearColor::Transparent;
	Target->TargetGamma = 1.f;  // the canvas writes linear colour; the sRGB target encodes it
	Target->AddressX = TA_Clamp;
	Target->AddressY = TA_Clamp;
	Target->InitAutoFormat(FMath::CeilToInt(Size.X * TexelsPerBasePixel), FMath::CeilToInt(Size.Y * TexelsPerBasePixel));
	Target->UpdateResourceImmediate(true);
	// exact values (ids), so linear and unfiltered
	CodeTarget = NewObject<UTextureRenderTarget2D>(this);
	CodeTarget->RenderTargetFormat = RTF_RGBA8;
	CodeTarget->bForceLinearGamma = true;
	CodeTarget->TargetGamma = 1.f;
	CodeTarget->ClearColor = FLinearColor::Transparent;
	CodeTarget->Filter = TF_Nearest;
	CodeTarget->AddressX = TA_Clamp;
	CodeTarget->AddressY = TA_Clamp;
	CodeTarget->InitCustomFormat(Target->SizeX, Target->SizeY, PF_B8G8R8A8, true);
	CodeTarget->UpdateResourceImmediate(true);
}

void UMRSpriteBodyComponent::ApplyMaterial()
{
	bUnlit = WantsUnlit();
	const FString Dir = FMRSpriteLibrary::Get().TextureDir;
	const FString Name = bUnlit ? TEXT("M_SpriteBodyUnlit") : TEXT("M_SpriteBody");
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Dir, *Name, *Name));
	if (!Base)
	{
		UE_LOG(LogMeridian, Warning, TEXT("%s/%s missing (run tools/ue/import_sprites.ps1)"), *Dir, *Name);
		return;
	}
	Material = UMaterialInstanceDynamic::Create(Base, this);
	Material->SetTextureParameterValue(TEXT("Sprite"), Target);
	Material->SetTextureParameterValue(TEXT("SpriteCode"), CodeTarget);
	const TArray<FMRSpriteMaterialClass>& Classes = FMRSpriteLibrary::Get().MaterialClasses;
	for (int32 i = 0; i < Classes.Num() && i < 8; ++i)
	{
		Material->SetVectorParameterValue(*FString::Printf(TEXT("Class%d"), i),
			FLinearColor(Classes[i].Roughness, Classes[i].Metallic, Classes[i].Specular, 0.f));
	}
	LastAlbedo = LastNormalUp = LastAmbient = -1.f;
	ApplyLightingParams();
	TArray<FString> UV;
	CVarSpriteUV.GetValueOnGameThread().ParseIntoArrayWS(UV);
	const TCHAR* Params[] = {TEXT("SwapUV"), TEXT("FlipU"), TEXT("FlipV")};
	for (int32 i = 0; i < 3; ++i)
	{
		Material->SetScalarParameterValue(Params[i], UV.IsValidIndex(i) ? FCString::Atof(*UV[i]) : 0.f);
	}
	SetMaterial(0, Material);
	if (ShadowCard)
	{
		ShadowCard->SetMaterial(0, Material);
	}
}

bool UMRSpriteBodyComponent::IsReady(const UTexture2D* Tex)
{
	if (!Tex || !Tex->GetResource())
	{
		return false;
	}
#if WITH_EDITOR
	// in an editor build a just-loaded texture may still be compiling (a placeholder until it's done)
	if (Tex->IsDefaultTexture())
	{
		return false;
	}
#endif
	return true;
}

void UMRSpriteBodyComponent::PrewarmLook(FName LookName)
{
	if (const FMRSpriteLook* L = FMRSpriteLibrary::Get().Looks.Find(LookName))
	{
		for (const FMRSpritePart& Part : L->Parts)
		{
			AtlasTexture(Part.Atlas);
		}
	}
}

UTexture2D* UMRSpriteBodyComponent::AtlasTexture(const FString& Key)
{
	if (const TObjectPtr<UTexture2D>* Found = Textures.Find(Key))
	{
		return *Found;
	}
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	UTexture2D* Tex = nullptr;
	if (const FMRSpriteAtlas* Atlas = Lib.Atlases.Find(Key))
	{
		Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Lib.TextureDir, *Atlas->Texture, *Atlas->Texture));
		if (!Tex)
		{
			UE_LOG(LogMeridian, Warning, TEXT("Sprite atlas %s not imported (tools/ue/import_sprites.ps1)"), *Atlas->Texture);
		}
	}
	Textures.Add(Key, Tex);
	return Tex;
}

// ------------------------------------------------------------------------------ animation

void UMRSpriteBodyComponent::StartTracks(const FMRSpriteAction& Action, TMap<FName, FMRSpriteTrack>& Into)
{
	Into.Reset();
	const bool bWeapon = Look && Look->Find(WeaponName) != nullptr;
	for (const TPair<FName, FMRSpriteTrackDef>& T : Action.Tracks)
	{
		if (T.Key == WeaponName && !bWeapon)
		{
			continue;
		}
		FMRSpriteTrackDef Def = T.Value;
		if (!bWeapon && T.Key == RightArm && Def.Final == 17)
		{
			Def.Final = 1;  // the arm only rests on group 17 when it holds a weapon
		}
		Into.Add(T.Key).Start(Def);
	}
}

void UMRSpriteBodyComponent::SetBaseAction(FName Action)
{
	if (BaseAction == Action && BaseTracks.Num() > 0)
	{
		return;
	}
	BaseAction = Action;
	if (const FMRSpriteAction* A = FMRSpriteLibrary::Get().FindAction(Look ? Look->Name : NAME_None, Action))
	{
		StartTracks(*A, BaseTracks);
	}
	else
	{
		BaseTracks.Reset();
	}
}

void UMRSpriteBodyComponent::PlayAction(FName Action)
{
	const FMRSpriteAction* A = FMRSpriteLibrary::Get().FindAction(Look ? Look->Name : NAME_None, Action);
	if (!A || !Look)
	{
		return;
	}
	StartTracks(*A, ActionTracks);
	CurrentAction = Action;
	bActionLoops = A->OnceLengthMs() == 0;
	if (FirstPersonDef && (Action == TEXT("fist_attack") || Action == TEXT("weapon_attack")))
	{
		FirstPersonTrack.Start(FirstPersonDef->Attack);
	}
}

void UMRSpriteBodyComponent::StopAction()
{
	ActionTracks.Reset();
	CurrentAction = NAME_None;
}

TMap<FName, int32> UMRSpriteBodyComponent::CurrentGroups() const
{
	// SendOverlays / SendMoveOverlays: what each part shows when nothing animates it
	TMap<FName, int32> Groups;
	const bool bWeapon = Look->Find(WeaponName) != nullptr;
	for (const FMRSpritePart& Part : Look->Parts)
	{
		int32 G = 1;
		if (Part.Name == EyesName || Part.Name == MouthName)
		{
			G = Look->ActionFace;
		}
		else if (Part.Name == WeaponName)
		{
			G = 4;
		}
		else if (Part.Name == RightArm && bWeapon)
		{
			G = 17;
		}
		const FMRSpriteTrack* Base = BaseTracks.Find(Part.Name);
		if (Base && !(Part.Name == RightArm && bWeapon))  // a weapon arm doesn't swing
		{
			G = Base->Group;
		}
		if (const FMRSpriteTrack* Act = ActionTracks.Find(Part.Name))
		{
			G = Act->Group;
		}
		Groups.Add(Part.Name, G - 1);
	}
	return Groups;
}

const FMRSpriteTrack* UMRSpriteBodyComponent::TrackFor(FName Part) const
{
	if (const FMRSpriteTrack* Act = ActionTracks.Find(Part))
	{
		return Act;
	}
	if (Part == RightArm && Look->Find(WeaponName))
	{
		return nullptr;  // a weapon arm doesn't swing
	}
	return BaseTracks.Find(Part);
}

// ------------------------------------------------------------------------------ drawing

void UMRSpriteBodyComponent::Compose(int32 Angle, float DeltaTime)
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	const TMap<FName, int32> Groups = CurrentGroups();
	const bool bTweens = CVarSmoothTweens.GetValueOnGameThread() != 0;
	const float Window = FMath::Clamp(CVarSmoothCrossfade.GetValueOnGameThread(), 0.f, 1.f);

	// Smoothing B/C: a part between two poses shows the in-between for its phase (k of n);
	// smoothing A: in the last Window of each frame it fades into the next one.
	TMap<FName, int32> Current, Next;
	TMap<FName, float> Weight;
	for (const FMRSpritePart& Part : Look->Parts)
	{
		const FMRSpriteTrack* Tr = TrackFor(Part.Name);
		const FMRSpriteBgf* Bgf = Lib.FindBgf(Part.Bgf);
		if (!Tr || !Tr->IsPlaying() || !Bgf)
		{
			continue;
		}
		const int32 A = Bgf->BitmapIndex(Groups.FindRef(Part.Name), Angle);
		const int32 B = Bgf->BitmapIndex(Tr->NextGroup() - 1, Angle);
		if (A == INDEX_NONE || B == INDEX_NONE || A == B)
		{
			continue;
		}
		const TArray<int32>* Tw = bTweens ? Bgf->FindTweens(A, B) : nullptr;
		const int32 N = Tw ? Tw->Num() : 0;
		// a one-shot holds each pose, then moves on in its last OnceWindow (a cycle moves all the time)
		float Phase = Tr->Phase();
		if (Tr->Def.Mode == FMRSpriteTrackDef::EMode::Once)
		{
			const float W = FMath::Clamp(CVarSmoothOnceWindow.GetValueOnGameThread(), 0.01f, 1.f);
			Phase = FMath::Clamp((Phase - (1.f - W)) / W, 0.f, 1.f);
		}
		const float Sub = Phase * (N + 1);
		const int32 K = FMath::Clamp(FMath::FloorToInt(Sub), 0, N);
		const int32 Cur = K == 0 ? A : (*Tw)[K - 1];
		const int32 Nxt = K < N ? (*Tw)[K] : B;
		if (Cur != A)
		{
			Current.Add(Part.Name, Cur);
		}
		if (Window > 0.f)
		{
			const float W = FMath::SmoothStep(1.f - Window, 1.f, Sub - K);
			if (W > 0.02f)
			{
				Next.Add(Part.Name, Nxt);
				Weight.Add(Part.Name, W);
			}
		}
	}

	TArray<FMRSpritePlaced> Placed, PlacedNext;
	FVector2f Feet = FVector2f::ZeroVector, FeetNext = FVector2f::ZeroVector;
	int32 Shrink = 4;
	const bool bBackArmsUnder = CVarBackArmsUnder.GetValueOnGameThread() != 0;
	if (!Lib.Place(*Look, Groups, Angle, Placed, Feet, Shrink, &Current, bBackArmsUnder))
	{
		return;
	}
	LastShrink = Shrink;
	if (Weight.Num() > 0)
	{
		TMap<FName, int32> Both = Current;
		Both.Append(Next);
		Lib.Place(*Look, Groups, Angle, PlacedNext, FeetNext, Shrink, &Both, bBackArmsUnder);
	}

	// smoothing E: a new view fades in over the old one
	const int32 Slot = FMRSpriteLibrary::ViewSlot(Angle, 8);
	const float FadeTime = CVarSmoothAngleFade.GetValueOnGameThread();
	if (LastSlot != INDEX_NONE && Slot != LastSlot && FadeTime > 0.f && LastPlaced.Num() > 0)
	{
		FadeFrom = LastPlaced;
		FadeFromFeet = LastFeet;
		FadeElapsed = 0.f;
	}
	LastSlot = Slot;
	FadeElapsed += DeltaTime;
	const float Fade = FadeTime > 0.f ? FMath::Clamp(FadeElapsed / FadeTime, 0.f, 1.f) : 1.f;

	TArray<FDrawItem> Items;
	auto Add = [&Items](const FMRSpritePlaced& P, FVector2f F, float Alpha)
	{
		if (Alpha > 0.01f)
		{
			Items.Add({P.PartDef, P.Bitmap, P.Pos, P.Scale, F, Alpha});
		}
	};
	if (Fade < 1.f)
	{
		for (const FMRSpritePlaced& P : FadeFrom)
		{
			Add(P, FadeFromFeet, 1.f - Fade);
		}
	}
	for (const FMRSpritePlaced& P : Placed)
	{
		const float* W = Weight.Find(P.Part);
		Add(P, Feet, (W ? 1.f - *W : 1.f) * Fade);
		if (W)
		{
			if (const FMRSpritePlaced* N = PlacedNext.FindByPredicate([&P](const FMRSpritePlaced& X) { return X.Part == P.Part; }))
			{
				Add(*N, FeetNext, *W * Fade);
			}
		}
	}
	LastPlaced = Placed;
	LastFeet = Feet;
	DrawItems(Items);
}

void UMRSpriteBodyComponent::DrawItems(const TArray<FDrawItem>& Items)
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	uint32 Key = 1;
	for (const FDrawItem& I : Items)
	{
		Key = HashCombine(Key, HashCombine(::GetTypeHash(I.PartDef), ::GetTypeHash(I.Bitmap)));
		Key = HashCombine(Key, ::GetTypeHash(FMath::RoundToInt((I.Pos.X - I.Feet.X) * 16.f) * 65536 + FMath::RoundToInt((I.Pos.Y - I.Feet.Y) * 16.f)));
		Key = HashCombine(Key, ::GetTypeHash(FMath::RoundToInt(I.Alpha * 64.f)));
		Key = HashCombine(Key, ::GetTypeHash(PartXlat.FindRef(I.PartDef->Name)));
	}
	if (Key == LastDrawKey || !Target || !CodeTarget)
	{
		return;
	}
	LastDrawKey = Key;

	// the colour pass: the parts alpha-tested (crisp, like the original), so the atlas alpha - the
	// texel's source palette ramp, for M_SpriteBody's recolouring - lands in the target unblended
	UKismetRenderingLibrary::ClearRenderTarget2D(this, Target, FLinearColor::Transparent);
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, CanvasSize, Context);
	if (!Canvas)
	{
		return;
	}
	const float T = TexelsPerBasePixel;
	for (const FDrawItem& P : Items)
	{
		const FVector2f Origin = P.Feet + Look->Bounds.Min;
		const FMRSpriteAtlas* Atlas = Lib.Atlases.Find(P.PartDef->Atlas);
		const FIntRect* Cell = Atlas ? Atlas->Cells.Find(P.Bitmap) : nullptr;
		const FMRSpriteBgf* Bgf = Lib.FindBgf(P.PartDef->Bgf);
		UTexture2D* Tex = Cell ? AtlasTexture(P.PartDef->Atlas) : nullptr;
		if (!IsReady(Tex) || !Bgf)
		{
			LastDrawKey = 0;  // a texture not ready yet (just loaded): draw again next frame
			continue;
		}
		const FMRSpriteBitmap& Bm = Bgf->Bitmaps[P.Bitmap];
		const FVector2D Pos((P.Pos.X - Origin.X) * T, (P.Pos.Y - Origin.Y) * T);
		const FVector2D Size(Bm.W * P.Scale * T, Bm.H * P.Scale * T);
		const FVector2D AtlasSize(Atlas->Size.X, Atlas->Size.Y);
		FCanvasTileItem Item(Pos, Tex->GetResource(), Size,
			FVector2D(Cell->Min.X, Cell->Min.Y) / AtlasSize, FVector2D(Cell->Max.X, Cell->Max.Y) / AtlasSize, FLinearColor::White);
		Item.BlendMode = SE_BLEND_Masked;  // clip at 0.5, write colour and alpha as they are
		if (P.Alpha >= 0.5f)  // crossfades switch halfway (the parts can't blend alpha-tested)
		{
			Canvas->DrawItem(Item);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

	// the code pass: each part's translation and surface class, exact (alpha-tested at 0.5, no
	// blending), later parts over earlier ones as in the colour pass
	UKismetRenderingLibrary::ClearRenderTarget2D(this, CodeTarget, FLinearColor::Transparent);
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, CodeTarget, Canvas, CanvasSize, Context);
	if (!Canvas)
	{
		return;
	}
	for (const FDrawItem& P : Items)
	{
		if (P.Alpha < 0.5f)
		{
			continue;  // fading out (crossfades): the colour pass alone carries it
		}
		const FVector2f Origin = P.Feet + Look->Bounds.Min;
		const FMRSpriteAtlas* Atlas = Lib.Atlases.Find(P.PartDef->Atlas);
		const FIntRect* Cell = Atlas ? Atlas->Cells.Find(P.Bitmap) : nullptr;
		const FMRSpriteBgf* Bgf = Lib.FindBgf(P.PartDef->Bgf);
		UTexture2D* Tex = Cell ? AtlasTexture(P.PartDef->Atlas) : nullptr;
		if (!IsReady(Tex) || !Bgf)
		{
			LastDrawKey = 0;  // a texture not ready yet (just loaded): draw again next frame
			continue;
		}
		const FMRSpriteBitmap& Bm = Bgf->Bitmaps[P.Bitmap];
		const FVector2D AtlasSize(Atlas->Size.X, Atlas->Size.Y);
		const int32 Xlat = PartXlat.Contains(P.PartDef->Name) ? PartXlat[P.PartDef->Name] : P.PartDef->Xlat;
		FCanvasTileItem Item(FVector2D((P.Pos.X - Origin.X) * T, (P.Pos.Y - Origin.Y) * T), Tex->GetResource(),
			FVector2D(Bm.W * P.Scale * T, Bm.H * P.Scale * T),
			FVector2D(Cell->Min.X, Cell->Min.Y) / AtlasSize, FVector2D(Cell->Max.X, Cell->Max.Y) / AtlasSize,
			FLinearColor(Xlat / 255.f, P.PartDef->Class / 255.f, 0.f, 1.f));
		Item.BlendMode = SE_BLEND_MaskedDistanceField;  // writes the colour where the atlas alpha > 0.5
		Canvas->DrawItem(Item);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void UMRSpriteBodyComponent::PlaceQuad(UStaticMeshComponent* Quad, float FaceYaw, float Lean)
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	const float CmPerPx = Lib.CmPerBasePixel(LastShrink) * HeightScale;
	const FBox2f& B = Look->Bounds;
	const float WidthCm = B.GetSize().X * CmPerPx;
	const float HeightCm = B.GetSize().Y * CmPerPx;

	// the quad's normal points back along FaceYaw, leaning back by Lean radians (forward if negative)
	const FVector Flat = FRotator(0.f, FaceYaw, 0.f).Vector();
	const FVector Facing = Flat * FMath::Cos(Lean) + FVector::UpVector * FMath::Sin(Lean);
	FVector Up = -Flat * FMath::Sin(Lean) + FVector::UpVector * FMath::Cos(Lean);
	FVector Right = (Up ^ Facing).GetSafeNormal() * -1.f;  // the viewer's right
	const bool bMain = Quad == this;
	if (bMain && MotionRollDeg != 0.f)
	{
		// lean (procedural motion) about the feet
		const float R = FMath::DegreesToRadians(MotionRollDeg);
		const FVector U2 = Up * FMath::Cos(R) + Right * FMath::Sin(R);
		Right = Right * FMath::Cos(R) - Up * FMath::Sin(R);
		Up = U2;
	}
	const float ScaleZ = bMain ? MotionScaleZ : 1.f;
	const FRotator Rot = FRotationMatrix::MakeFromZX(Facing, Up).Rotator();

	const ACharacter* Char = Cast<ACharacter>(GetOwner());
	const float HalfHeight = Char ? Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector Feet = GetOwner()->GetActorLocation() - FVector(0.f, 0.f, HalfHeight - (bMain ? MotionBobCm : 0.f));
	// bounds are around the feet, y down
	const FVector Centre = Feet + Right * (B.GetCenter().X * CmPerPx) + Up * (-B.GetCenter().Y * CmPerPx * ScaleZ);
	if (Quad == ShadowCard)
	{
		ShadowFeetOffset = -B.GetCenter().Y * CmPerPx;
	}
	Quad->SetWorldLocationAndRotation(Centre, Rot);
	Quad->SetWorldScale3D(FVector(HeightCm * ScaleZ / 100.f, WidthCm / 100.f, 1.f));
}

void UMRSpriteBodyComponent::UpdateMotion(float DeltaTime, bool bMoving)
{
	const float Amount = FMath::Max(0.f, CVarSmoothMotion.GetValueOnGameThread());
	const AActor* Owner = GetOwner();
	const ACharacter* Char = Cast<ACharacter>(Owner);
	MotionTime += DeltaTime;
	float Bob = 0.f, ScaleZ = 1.f, Roll = 0.f;
	if (Amount > 0.f)
	{
		// walk bob: two bumps per leg cycle (one per step), from the legs' own animation
		const FMRSpriteTrack* Legs = TrackFor(TEXT("legs"));
		if (bMoving && Legs && Legs->IsPlaying())
		{
			const int32 N = FMath::Max(1, Legs->Def.High - Legs->Def.Low + 1);
			const float Cycle = ((Legs->Group - Legs->Def.Low) + Legs->Phase()) / N;
			Bob = 2.5f * FMath::Abs(FMath::Sin(Cycle * UE_TWO_PI));
		}
		// idle breathing
		if (!bMoving && CurrentAction.IsNone())
		{
			ScaleZ *= 1.f + 0.012f * FMath::Sin(MotionTime * UE_TWO_PI / 3.2f);
		}
		// leaning into turns: toward the character's side of the turn, as the viewer sees it
		const float Yaw = Owner->GetActorRotation().Yaw;
		const float YawRate = DeltaTime > 0.f ? FRotator::NormalizeAxis(Yaw - LastYaw) / DeltaTime : 0.f;
		LastYaw = Yaw;
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				const FVector ViewRight = FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y);
				const float Side = Owner->GetActorRightVector() | ViewRight;
				Roll = FMath::Clamp(YawRate * 0.02f, -6.f, 6.f) * Side * (bMoving ? 1.f : 0.3f);
			}
		}
		// landing squash, take-off stretch
		const bool bFalling = Char && Char->GetMovementComponent() && Char->GetMovementComponent()->IsFalling();
		if (bWasFalling && !bFalling)
		{
			SquashTimer = 0.18f;
		}
		bWasFalling = bFalling;
		SquashTimer = FMath::Max(0.f, SquashTimer - DeltaTime);
		ScaleZ *= 1.f - 0.08f * (SquashTimer / 0.18f);
		if (bFalling && Owner->GetVelocity().Z > 100.f)
		{
			ScaleZ *= 1.03f;
		}
	}
	MotionBobCm = Bob * Amount;
	MotionScaleZ = 1.f + (ScaleZ - 1.f) * Amount;
	MotionRollDeg = FMath::FInterpTo(MotionRollDeg, Roll * Amount, DeltaTime, 8.f);
}

void UMRSpriteBodyComponent::UpdateSun(float DeltaTime)
{
	SunCheckTimer -= DeltaTime;
	if (SunCheckTimer > 0.f)
	{
		return;
	}
	SunCheckTimer = 1.f;
	float Best = -1.f;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		const ULightComponent* L = It->GetLightComponent();
		if (L && L->IsVisible() && L->Intensity > Best)
		{
			Best = L->Intensity;
			SunYaw = (-It->GetActorForwardVector()).Rotation().Yaw;
			SunDir = It->GetActorForwardVector();
		}
	}
}

void UMRSpriteBodyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	if (!Look || !Owner)
	{
		return;
	}
	if (bUnlit != WantsUnlit())
	{
		ApplyMaterial();
	}
	ApplyLightingParams();

	// movement -> stand / walk; the walk cycle follows the ground speed
	const FVector Vel = Owner->GetVelocity();
	const float Speed = FVector(Vel.X, Vel.Y, 0.f).Size();
	const ACharacter* Char = Cast<ACharacter>(Owner);
	const bool bMoving = Speed > MovingSpeed && (!Char || !Char->GetMovementComponent() || Char->GetMovementComponent()->IsMovingOnGround());
	SetBaseAction(bMoving ? WalkName : StandName);
	if (bMoving && bActionLoops && !CurrentAction.IsNone())
	{
		StopAction();  // dancing stops when you walk away
	}
	const float DtMs = DeltaTime * 1000.f;
	// monsters (their own Kod animations) walk at the original rate, as the original client did
	const float WalkRefSetting = CVarSpriteWalkRef.GetValueOnGameThread();
	const float WalkRef = WalkRefSetting < 0.f ? UMRCharacterMovementComponent::RunCms() : WalkRefSetting;
	const bool bOwnActions = FMRSpriteLibrary::Get().LookActions.Contains(Look->Name);
	const float WalkRate = WalkRef > 0.f && !bOwnActions ? FMath::Clamp(Speed / WalkRef, 0.4f, 2.5f) : 1.f;
	for (TPair<FName, FMRSpriteTrack>& T : BaseTracks)
	{
		T.Value.Step(bMoving ? DtMs * WalkRate : DtMs);
	}
	bool bActionPlaying = false;
	for (TPair<FName, FMRSpriteTrack>& T : ActionTracks)
	{
		T.Value.Step(DtMs);
		bActionPlaying |= T.Value.IsPlaying();
	}
	if (!bActionPlaying && !CurrentAction.IsNone())
	{
		StopAction();
	}
	FirstPersonTrack.Step(DtMs);

	// the viewer: this machine's camera (or SetViewer's)
	FVector ViewLoc = Owner->GetActorLocation() + Owner->GetActorForwardVector() * 300.f;
	FRotator ViewRot = (Owner->GetActorLocation() - ViewLoc).Rotation();
	if (const USceneComponent* V = Viewer.Get())
	{
		ViewLoc = V->GetComponentLocation();
		ViewRot = V->GetComponentRotation();
	}
	else if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			ViewLoc = PC->PlayerCameraManager->GetCameraLocation();
			ViewRot = PC->PlayerCameraManager->GetCameraRotation();
		}
	}
	const FVector ToViewer = ViewLoc - Owner->GetActorLocation();
	const float ViewerYaw = ToViewer.Rotation().Yaw;
	const int32 Angle = FMRSpriteLibrary::RelativeAngle(Owner->GetActorRotation().Yaw, ViewerYaw);
	LastAngle = Angle;

	EnsureTarget();
	Compose(Angle, DeltaTime);
	UpdateMotion(DeltaTime, bMoving);
	// Like the original (d3drender.c: every object quad is turned by the camera's heading only): the
	// quad is parallel to the screen, not turned toward the camera's position, so it projects without
	// perspective stretch anywhere on screen; and it follows the camera's pitch (the original could
	// only look about 11 degrees up or down), so seen from above it isn't squashed either.
	const float Lean = FMath::DegreesToRadians(-FRotator::NormalizeAxis(ViewRot.Pitch)) * FMath::Clamp(CVarSpriteBillboard.GetValueOnGameThread(), 0.f, 1.f);
	PlaceQuad(this, ViewRot.Yaw + 180.f, Lean);

	UpdateSun(DeltaTime);
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.f));  // shading and shadow push
	}
	if (ShadowCard)
	{
		const bool bShadow = CVarSpriteShadow.GetValueOnGameThread() == 1;
		ShadowCard->SetCastShadow(bShadow);
		if (bShadow)
		{
			PlaceQuad(ShadowCard, SunYaw, 0.f);
			if (Material)
			{
				// shadow pass push (M_SpriteBody): along the light, from the feet up
				Material->SetScalarParameterValue(TEXT("FeetZ"), ShadowCard->GetComponentLocation().Z - ShadowFeetOffset);
			}
		}
	}
}

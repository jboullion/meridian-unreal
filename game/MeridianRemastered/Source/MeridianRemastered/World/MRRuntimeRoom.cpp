#include "World/MRRuntimeRoom.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeridianRemastered.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"
#include "World/MRRoomMesh.h"

namespace
{
	const TCHAR* MaterialPath = TEXT("/Game/Generated/Runtime/M_RuntimeRoom.M_RuntimeRoom");

	void ToSection(const FMRRoomMeshSection& S, TArray<FLinearColor>& OutColors)
	{
		OutColors.Reset(S.Light.Num());
		for (const float L : S.Light)
		{
			OutColors.Add(FLinearColor(L, L, L, 1.f));
		}
	}
}

AMRRuntimeRoom::AMRRuntimeRoom()
{
	PrimaryActorTick.bCanEverTick = false;
	RenderMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Render"));
	RootComponent = RenderMeshComponent;
	RenderMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RenderMeshComponent->SetCastShadow(true);

	CollisionMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Collision"));
	CollisionMeshComponent->SetupAttachment(RenderMeshComponent);
	CollisionMeshComponent->SetVisibility(false);
	CollisionMeshComponent->SetHiddenInGame(true);
	CollisionMeshComponent->bUseComplexAsSimpleCollision = true;
	CollisionMeshComponent->bUseAsyncCooking = false;  // the pawn is placed on it right away
	CollisionMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	CollisionMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionMeshComponent->SetCanEverAffectNavigation(false);
}

UMaterialInterface* AMRRuntimeRoom::LoadMaterial()
{
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
	if (!M)
	{
		UE_LOG(LogMeridian, Error, TEXT("World: %s missing (tools/ue/build_world.ps1 builds it)"), MaterialPath);
	}
	return M;
}

void AMRRuntimeRoom::Build(const FMRRoomMesh& RenderMesh, const FMRRoomMesh& CollisionMesh, const TMap<uint16, UTexture2D*>& InTextures,
	UMaterialInterface* Material)
{
	Textures.Reset();
	RenderMeshComponent->ClearAllMeshSections();
	TArray<FLinearColor> Colors;
	for (int32 i = 0; i < RenderMesh.Sections.Num(); ++i)
	{
		const FMRRoomMeshSection& S = RenderMesh.Sections[i];
		ToSection(S, Colors);
		RenderMeshComponent->CreateMeshSection_LinearColor(i, S.Positions, S.Triangles, S.Normals, S.UVs, Colors, TArray<FProcMeshTangent>(), false);
		UTexture2D* Tex = InTextures.FindRef(S.Texture);
		if (Material)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
			if (Tex)
			{
				MID->SetTextureParameterValue(TEXT("Tex"), Tex);
			}
			RenderMeshComponent->SetMaterial(i, MID);
		}
		if (Tex)
		{
			Textures.Add(Tex);
		}
	}

	// one section for collision: every surface the original moves you on
	FMRRoomMeshSection All;
	for (const FMRRoomMeshSection& S : CollisionMesh.Sections)
	{
		const int32 Base = All.Positions.Num();
		All.Positions.Append(S.Positions);
		All.Normals.Append(S.Normals);
		All.UVs.Append(S.UVs);
		All.Light.Append(S.Light);
		for (const int32 T : S.Triangles)
		{
			All.Triangles.Add(Base + T);
		}
	}
	CollisionMeshComponent->ClearAllMeshSections();
	if (UBodySetup* Body = CollisionMeshComponent->GetBodySetup())
	{
		Body->bDoubleSidedGeometry = true;  // walls are touched from both sides, as in the original (set before cooking)
	}
	ToSection(All, Colors);
	CollisionMeshComponent->CreateMeshSection_LinearColor(0, All.Positions, All.Triangles, All.Normals, All.UVs, Colors, TArray<FProcMeshTangent>(), true);
	UE_LOG(LogMeridian, Log, TEXT("World: built %s: %d triangles in %d textures, %d for collision"), *RoomFile,
		RenderMesh.NumTriangles(), RenderMesh.Sections.Num(), CollisionMesh.NumTriangles());
}

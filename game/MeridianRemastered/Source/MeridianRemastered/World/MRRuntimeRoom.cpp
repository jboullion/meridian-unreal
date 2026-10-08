#include "World/MRRuntimeRoom.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeridianRemastered.h"
#include "Net/MRProtocol.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"
#include "World/MRRoomMesh.h"

namespace
{
	const TCHAR* MaterialPath = TEXT("/Game/Generated/Runtime/M_RuntimeRoom.M_RuntimeRoom");

	// a moving lift redraws this often (the original every frame; the mesher takes a few ms on a big room),
	// and its collision (cooked off the game thread meanwhile) this often; both exact when it stops
	constexpr double LiftDrawSeconds = 1.0 / 20.0;
	constexpr double LiftCollisionSeconds = 0.2;

	void ToColors(const FMRRoomMeshSection& S, TArray<FLinearColor>& OutColors)
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
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;  // only while a lift moves
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

void AMRRuntimeRoom::Build(const FMRRooFile& InRoom, const TMap<uint16, FVector2D>& InRepeats, const TMap<uint16, UTexture2D*>& InTextures,
	UMaterialInterface* Material)
{
	Original = InRoom;
	Room = InRoom;
	Repeats = InRepeats;
	BaseMaterial = Material;
	Textures.Reset();
	TextureByNumber.Reset();
	for (const TPair<uint16, UTexture2D*>& Pair : InTextures)
	{
		if (Pair.Value)
		{
			TextureByNumber.Add(Pair.Key, Pair.Value);
			Textures.Add(Pair.Value);
		}
	}
	Materials.Reset();
	MaterialByTexture.Reset();
	Lifts.Reset();
	bChanged = false;
	Rebuild(true);
}

UMaterialInstanceDynamic* AMRRuntimeRoom::MaterialFor(uint16 Texture)
{
	if (UMaterialInstanceDynamic** Found = MaterialByTexture.Find(Texture))
	{
		return *Found;
	}
	if (!BaseMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (UTexture2D* Tex = TextureByNumber.FindRef(Texture))
	{
		MID->SetTextureParameterValue(TEXT("Tex"), Tex);
	}
	Materials.Add(MID);
	MaterialByTexture.Add(Texture, MID);
	return MID;
}

void AMRRuntimeRoom::Rebuild(bool bCollision)
{
	const double Start = FPlatformTime::Seconds();
	auto Repeat = [this](uint16 Texture)
	{
		const FVector2D* R = Repeats.Find(Texture);
		return R ? *R : FVector2D(MRRoo::RooPerSquare, MRRoo::RooPerSquare);
	};
	const FMRRoomMesh RenderMesh = MRRoomMesh::Build(Room, Repeat, false);
	RenderMeshComponent->ClearAllMeshSections();
	TArray<FLinearColor> Colors;
	const TArray<FVector2D> NoUVs;
	for (int32 i = 0; i < RenderMesh.Sections.Num(); ++i)
	{
		const FMRRoomMeshSection& S = RenderMesh.Sections[i];
		ToColors(S, Colors);
		// UV1: how fast the texture scrolls there (M_RuntimeRoom adds Time * UV1)
		RenderMeshComponent->CreateMeshSection_LinearColor(i, S.Positions, S.Triangles, S.Normals, S.UVs, S.Scroll, NoUVs, NoUVs, Colors,
			TArray<FProcMeshTangent>(), false);
		RenderMeshComponent->SetMaterial(i, MaterialFor(S.Texture));
	}

	int32 CollisionTriangles = -1;
	if (bCollision)
	{
		// one section for collision: every surface the original moves you on
		const FMRRoomMesh CollisionMesh = MRRoomMesh::Build(Room, Repeat, true);
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
		ToColors(All, Colors);
		CollisionMeshComponent->CreateMeshSection_LinearColor(0, All.Positions, All.Triangles, All.Normals, All.UVs, Colors, TArray<FProcMeshTangent>(), true);
		CollisionTriangles = CollisionMesh.NumTriangles();
	}
	if (Rebuilds++ == 0)
	{
		MeshBounds = RenderMesh.Bounds();
		UE_LOG(LogMeridian, Log, TEXT("World: built %s: %d triangles in %d textures, %d for collision (%.1f ms)"), *RoomFile,
			RenderMesh.NumTriangles(), RenderMesh.Sections.Num(), CollisionTriangles, (FPlatformTime::Seconds() - Start) * 1000.0);
	}
	else
	{
		UE_LOG(LogMeridian, Verbose, TEXT("World: rebuilt %s%s in %.1f ms"), *RoomFile, bCollision ? TEXT(" and its collision") : TEXT(""),
			(FPlatformTime::Seconds() - Start) * 1000.0);
	}
}

// ------------------------------------------------------------------------------ the server's changes

void AMRRuntimeRoom::ResetChanges()
{
	Lifts.Reset();
	SetActorTickEnabled(false);
	if (bChanged)
	{
		Room = Original;
		bChanged = false;
		Rebuild(true);
	}
}

void AMRRuntimeRoom::SetHeight(int32 Sector, bool bCeiling, int16 Height)
{
	FMRRooSector& S = Room.Sectors[Sector];
	(bCeiling ? S.CeilingHeight : S.FloorHeight) = Height;
}

void AMRRuntimeRoom::MoveSector(uint8 Type, uint16 SectorId, int16 Height, uint8 Speed)
{
	// roomanim.c MoveSector: every sector with the id; heights are Kod units (as the .roo's), speed Kod units a second
	if (Type != MRMsg::ANIMATE_FLOOR_LIFT && Type != MRMsg::ANIMATE_CEILING_LIFT)
	{
		UE_LOG(LogMeridian, Warning, TEXT("World: %s: unknown sector move %d"), *RoomFile, Type);
		return;
	}
	const bool bCeiling = Type == MRMsg::ANIMATE_CEILING_LIFT;
	bool bAtOnce = false;
	int32 Moved = 0;
	for (int32 i = 0; i < Room.Sectors.Num(); ++i)
	{
		FMRRooSector& S = Room.Sectors[i];
		if (S.ServerId != SectorId)
		{
			continue;
		}
		++Moved;
		bChanged = true;
		Lifts.RemoveAll([i, bCeiling](const FLift& L) { return L.Sector == i && L.bCeiling == bCeiling; });
		const int16 From = bCeiling ? S.CeilingHeight : S.FloorHeight;
		if (Speed == 0 || From == Height)
		{
			SetHeight(i, bCeiling, Height);
			bAtOnce = true;
			continue;
		}
		FLift& L = Lifts.AddDefaulted_GetRef();
		L.Sector = i;
		L.bCeiling = bCeiling;
		L.From = From;
		L.To = Height;
		L.Duration = FMath::Abs(static_cast<double>(Height) - From) / Speed;
	}
	UE_LOG(LogMeridian, Log, TEXT("World: %s: sector %d's %s to %d at %d (%d sectors)"), *RoomFile, SectorId, bCeiling ? TEXT("ceiling") : TEXT("floor"),
		Height, Speed, Moved);
	if (bAtOnce)
	{
		Rebuild(true);
	}
	if (Lifts.Num() > 0)
	{
		SinceDraw = SinceCollision = 0.0;
		SetActorTickEnabled(true);
	}
}

void AMRRuntimeRoom::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	bool bDone = false;
	for (int32 i = Lifts.Num() - 1; i >= 0; --i)
	{
		FLift& L = Lifts[i];
		L.Elapsed += DeltaSeconds;
		const double T = L.Duration > 0.0 ? FMath::Min(1.0, L.Elapsed / L.Duration) : 1.0;
		SetHeight(L.Sector, L.bCeiling, static_cast<int16>(FMath::RoundToInt(FMath::Lerp(L.From, L.To, T))));
		if (T >= 1.0)
		{
			Lifts.RemoveAt(i);
			bDone = true;
		}
	}
	SinceDraw += DeltaSeconds;
	SinceCollision += DeltaSeconds;
	const bool bStopped = Lifts.Num() == 0;
	if (bDone || bStopped || SinceDraw >= LiftDrawSeconds)
	{
		// the floor the pawn stands on moves with the drawn one (the original sets objects' z to the lift's floor)
		const bool bCollision = bStopped || SinceCollision >= LiftCollisionSeconds;
		CollisionMeshComponent->bUseAsyncCooking = !bStopped;
		Rebuild(bCollision);
		SinceDraw = 0.0;
		if (bCollision)
		{
			SinceCollision = 0.0;
		}
	}
	if (bStopped)
	{
		SetActorTickEnabled(false);
	}
}

void AMRRuntimeRoom::ChangeSector(uint16 SectorId, uint8 Depth, uint8 Scroll)
{
	// roomanim.c SectorChange: the depth bits, and the scroll speed keeping the direction and which surfaces scroll
	int32 Changed = 0;
	for (FMRRooSector& S : Room.Sectors)
	{
		if (S.ServerId != SectorId)
		{
			continue;
		}
		++Changed;
		if (Depth != MRMsg::CHANGE_OVERRIDE)
		{
			S.Flags = (S.Flags & ~MRRoo::SF_MASK_DEPTH) | (Depth & MRRoo::SF_MASK_DEPTH);
		}
		if (Scroll != MRMsg::CHANGE_OVERRIDE)
		{
			const uint32 Kept = Scroll ? (S.Flags & 0x1F0) : 0;  // direction, SF_SCROLL_FLOOR, SF_SCROLL_CEILING
			S.Flags = (S.Flags & ~0x1FCu) | (static_cast<uint32>(Scroll) << 2) | Kept;
		}
	}
	UE_LOG(LogMeridian, Log, TEXT("World: %s: sector %d depth %d scroll %d (%d sectors)"), *RoomFile, SectorId, Depth, Scroll, Changed);
	if (Changed)
	{
		bChanged = true;
		Rebuild(true);
	}
}

bool AMRRuntimeRoom::ChangeTexture(uint16 Id, uint16 Texture, uint8 Flags)
{
	// roomanim.c TextureChange: sidedefs and sectors with the id
	int32 Changed = 0;
	for (FMRRooSidedef& S : Room.Sidedefs)
	{
		if (S.ServerId != Id)
		{
			continue;
		}
		if (Flags & MRMsg::CTF_ABOVEWALL) { S.AboveTexture = Texture; ++Changed; }
		if (Flags & MRMsg::CTF_NORMALWALL) { S.NormalTexture = Texture; ++Changed; }
		if (Flags & MRMsg::CTF_BELOWWALL) { S.BelowTexture = Texture; ++Changed; }
	}
	for (FMRRooSector& S : Room.Sectors)
	{
		if (S.ServerId != Id)
		{
			continue;
		}
		if (Flags & MRMsg::CTF_FLOOR) { S.FloorTexture = Texture; ++Changed; }
		if (Flags & MRMsg::CTF_CEILING) { S.CeilingTexture = Texture; ++Changed; }
	}
	UE_LOG(LogMeridian, Log, TEXT("World: %s: id %d takes texture %d (flags %02x, %d surfaces)"), *RoomFile, Id, Texture, Flags, Changed);
	if (Changed)
	{
		bChanged = true;
		Rebuild(true);
	}
	return Texture == 0 || HasTexture(Texture);
}

void AMRRuntimeRoom::AddTexture(uint16 Texture, UTexture2D* Tex, const FVector2D& Repeat)
{
	if (!Tex)
	{
		return;
	}
	TextureByNumber.Add(Texture, Tex);
	Textures.Add(Tex);
	Repeats.Add(Texture, Repeat);
	if (UMaterialInstanceDynamic** MID = MaterialByTexture.Find(Texture))
	{
		(*MID)->SetTextureParameterValue(TEXT("Tex"), Tex);
	}
	Rebuild(false);  // the walls' UVs follow the texture's size
}

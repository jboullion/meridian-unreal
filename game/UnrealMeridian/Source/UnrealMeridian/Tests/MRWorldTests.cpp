// Automation tests for rooms built at runtime (docs/adr/0012-client-parity-and-world-coverage.md).
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian.World;Quit" -unattended -nullrhi
// They read the reference checkout (ReferenceServers/Server-104) and tools' build output (build/),
// so they only run on a development machine; without those files they pass with a warning.

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/MRBgf.h"
#include "World/MRRooFile.h"
#include "World/MRRoomMesh.h"
#include "World/MRRuntimeRoom.h"
#include "Audio/MRServerSound.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Net/MRProtocol.h"
#include "Zones/MRZoneSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString RepoDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("..")));
	}

	TSharedPtr<FJsonObject> LoadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	/** Triangles in a .glb written by tools/roo2gltf (every primitive's index count / 3). */
	int32 GlbTriangles(const FString& Path)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 20)
		{
			return -1;
		}
		const uint32 JsonLen = Bytes[12] | (Bytes[13] << 8) | (Bytes[14] << 16) | (Bytes[15] << 24);
		const FString Json(static_cast<int32>(JsonLen), reinterpret_cast<const UTF8CHAR*>(Bytes.GetData() + 20));
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
		{
			return -1;
		}
		const TArray<TSharedPtr<FJsonValue>>& Accessors = Root->GetArrayField(TEXT("accessors"));
		int32 Tris = 0;
		for (const TSharedPtr<FJsonValue>& M : Root->GetArrayField(TEXT("meshes")))
		{
			for (const TSharedPtr<FJsonValue>& P : M->AsObject()->GetArrayField(TEXT("primitives")))
			{
				const int32 Idx = static_cast<int32>(P->AsObject()->GetNumberField(TEXT("indices")));
				Tris += static_cast<int32>(Accessors[Idx]->AsObject()->GetNumberField(TEXT("count"))) / 3;
			}
		}
		return Tris;
	}

	/** Texture repeats as roo2gltf used them: build/textures/catalog.json (tools/bgf2png), else one per square. */
	MRRoomMesh::FRepeat CatalogRepeat()
	{
		TMap<uint16, FVector2D> Sizes;
		if (const TSharedPtr<FJsonObject> Cat = LoadJson(FPaths::Combine(RepoDir(), TEXT("build"), TEXT("textures"), TEXT("catalog.json"))))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Cat->GetObjectField(TEXT("textures"))->Values)
			{
				const TSharedPtr<FJsonObject> T = Pair.Value->AsObject();
				const double Shrink = T->GetNumberField(TEXT("shrink"));
				Sizes.Add(static_cast<uint16>(FCString::Atoi(*Pair.Key.Mid(3))),
					FVector2D(T->GetNumberField(TEXT("w")), T->GetNumberField(TEXT("h"))) / Shrink * MRRoo::RooPerFine);
			}
		}
		return [Sizes](uint16 Tex)
		{
			const FVector2D* S = Sizes.Find(Tex);
			return S ? *S : FVector2D(MRRoo::RooPerSquare, MRRoo::RooPerSquare);
		};
	}

	FString RoomsDir()
	{
		return FPaths::Combine(RepoDir(), TEXT("ReferenceServers"), TEXT("Server-104"), TEXT("resource"), TEXT("rooms"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRWorldRoomsTest, "Meridian.World.Rooms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRWorldRoomsTest::RunTest(const FString& Parameters)
{
	// 1. our zones: the C++ mesh is roo2gltf's, triangle for triangle (render and collision)
	const TSharedPtr<FJsonObject> Layout = LoadJson(FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("zone_layout.json")));
	if (!Layout.IsValid() || !IFileManager::Get().DirectoryExists(*RoomsDir()))
	{
		AddWarning(TEXT("no zone_layout.json or reference rooms: skipped"));
		return true;
	}
	const MRRoomMesh::FRepeat Repeat = CatalogRepeat();
	int32 Compared = 0;
	for (const TSharedPtr<FJsonValue>& V : Layout->GetArrayField(TEXT("zones")))
	{
		const TSharedPtr<FJsonObject> Z = V->AsObject();
		const FString Roo = Z->GetStringField(TEXT("roo"));
		const FString Stem = FString::Printf(TEXT("%d_%s"), static_cast<int32>(Z->GetNumberField(TEXT("rid"))), *Z->GetStringField(TEXT("class")));
		TArray<uint8> Bytes;
		FMRRooFile Room;
		FString Error;
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *RoomsDir(), TEXT("*.roo"));
		const FString* Match = Found.FindByPredicate([&Roo](const FString& F) { return F.Equals(Roo, ESearchCase::IgnoreCase); });
		if (!Match || !FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(RoomsDir(), *Match)) || !Room.Load(Bytes, Error))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Roo, Error.IsEmpty() ? TEXT("not found") : *Error));
			continue;
		}
		const double Security = Z->GetNumberField(TEXT("roo_security"));
		TestEqual(FString::Printf(TEXT("%s security"), *Roo), Room.Security, static_cast<uint32>(static_cast<int64>(Security)));
		const TArray<TSharedPtr<FJsonValue>>& Grid = Z->GetArrayField(TEXT("grid_size_roo"));
		TestEqual(FString::Printf(TEXT("%s grid width"), *Roo), Room.Width, static_cast<int32>(Grid[0]->AsNumber()));
		const int32 PyRender = GlbTriangles(FPaths::Combine(RepoDir(), TEXT("build"), TEXT("zones"), Stem + TEXT(".glb")));
		const int32 PyCollision = GlbTriangles(FPaths::Combine(RepoDir(), TEXT("build"), TEXT("zones"), Stem + TEXT("_collision.glb")));
		if (PyRender < 0 || PyCollision < 0)
		{
			AddWarning(FString::Printf(TEXT("%s: no roo2gltf output to compare with"), *Stem));
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s render triangles (roo2gltf)"), *Stem), MRRoomMesh::Build(Room, Repeat, false).NumTriangles(), PyRender);
		TestEqual(FString::Printf(TEXT("%s collision triangles (roo2gltf)"), *Stem), MRRoomMesh::Build(Room, Repeat, true).NumTriangles(), PyCollision);
		++Compared;
	}
	TestTrue(TEXT("compared some zones"), Compared > 0);

	// 2. every room in the reference checkout parses and builds
	TArray<FString> All;
	IFileManager::Get().FindFiles(All, *RoomsDir(), TEXT("*.roo"));
	int32 Built = 0, Tris = 0;
	const double Start = FPlatformTime::Seconds();
	for (const FString& F : All)
	{
		TArray<uint8> Bytes;
		FMRRooFile Room;
		FString Error;
		if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(RoomsDir(), F)) || !Room.Load(Bytes, Error))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *F, *Error));
			continue;
		}
		const int32 N = MRRoomMesh::Build(Room, Repeat, false).NumTriangles();
		if (N <= 0)
		{
			AddError(FString::Printf(TEXT("%s: no triangles"), *F));
			continue;
		}
		Tris += N;
		++Built;
	}
	AddInfo(FString::Printf(TEXT("%d of %d rooms built, %d triangles, %.1f s"), Built, All.Num(), Tris, FPlatformTime::Seconds() - Start));
	TestEqual(TEXT("every reference room builds"), Built, All.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRWorldChangesTest, "Meridian.World.Changes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRWorldChangesTest::RunTest(const FString& Parameters)
{
	// 1. scrolling (bspload.c, roomanim.c): only rooms with scrolling sectors or walls have UV1
	TArray<FString> All;
	IFileManager::Get().FindFiles(All, *RoomsDir(), TEXT("*.roo"));
	if (All.Num() == 0)
	{
		AddWarning(TEXT("no reference rooms: skipped"));
		return true;
	}
	All.Sort();
	const MRRoomMesh::FRepeat Repeat = CatalogRepeat();
	int32 Scrolling = 0, Still = 0;
	FMRRooFile Movable;
	FString MovableName;
	for (const FString& F : All)
	{
		TArray<uint8> Bytes;
		FMRRooFile Room;
		FString Error;
		if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(RoomsDir(), F)) || !Room.Load(Bytes, Error))
		{
			continue;
		}
		bool bFlagged = false;
		for (const FMRRooSector& S : Room.Sectors)
		{
			bFlagged |= !S.AnimationSpeed && (S.Flags & 0x0C) && (S.Flags & 0x180);
			if (MovableName.IsEmpty() && S.ServerId && S.FloorTexture && !S.bSlopedFloor)
			{
				MovableName = F;
				Movable = Room;
			}
		}
		for (const FMRRooSidedef& S : Room.Sidedefs)
		{
			bFlagged |= !S.AnimationSpeed && (S.Flags & 0x0C00);
		}
		bool bMoves = false;
		for (const FMRRoomMeshSection& Sec : MRRoomMesh::Build(Room, Repeat, false).Sections)
		{
			if (Sec.Scroll.Num() != Sec.Positions.Num())
			{
				AddError(FString::Printf(TEXT("%s: %d scrolls for %d vertices"), *F, Sec.Scroll.Num(), Sec.Positions.Num()));
			}
			for (const FVector2D& V : Sec.Scroll)
			{
				bMoves |= !V.IsNearlyZero();
			}
		}
		if (!bFlagged)
		{
			TestFalse(FString::Printf(TEXT("%s doesn't scroll"), *F), bMoves);
			++Still;
		}
		else if (bMoves)
		{
			++Scrolling;
		}
	}
	AddInfo(FString::Printf(TEXT("%d rooms scroll, %d don't"), Scrolling, Still));
	TestTrue(TEXT("some rooms scroll"), Scrolling > 0);
	if (MovableName.IsEmpty())
	{
		AddWarning(TEXT("no room with a server id sector"));
		return true;
	}
	const int32 Index = Movable.Sectors.IndexOfByPredicate([](const FMRRooSector& S) { return S.ServerId && S.FloorTexture && !S.bSlopedFloor; });
	{
		// a scrolling floor moves at its period's speed: fast is a step every 2 ms, 1024 steps to a repeat
		FMRRooFile One = Movable;
		for (FMRRooSector& S : One.Sectors)
		{
			S.Flags &= ~0x1FCu;
			S.AnimationSpeed = 0;
		}
		for (FMRRooSidedef& S : One.Sidedefs)
		{
			S.Flags &= ~0x7C00u;
		}
		One.Sectors[Index].Flags |= (3 << 2) | (4 << 4) | 0x80;  // fast, south, the floor
		double Seen = 0.0;
		for (const FMRRoomMeshSection& Sec : MRRoomMesh::Build(One, Repeat, false).Sections)
		{
			for (const FVector2D& V : Sec.Scroll)
			{
				Seen = FMath::Max(Seen, V.Length());
			}
		}
		TestTrue(FString::Printf(TEXT("a fast floor scrolls 1000/2/1024 a second (%f)"), Seen), FMath::IsNearlyEqual(Seen, 1000.0 / 2 / 1024, 1e-4));
	}

	{
		// what a lift's redraw costs: the mesher on this room
		const double Start = FPlatformTime::Seconds();
		for (int32 i = 0; i < 10; ++i)
		{
			MRRoomMesh::Build(Movable, Repeat, false);
		}
		AddInfo(FString::Printf(TEXT("%s meshes in %.1f ms"), *MovableName, (FPlatformTime::Seconds() - Start) * 100.0));
	}

	// 2. a runtime room takes the server's changes (roomanim.c MoveSector, SectorChange, TextureChange)
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	AMRRuntimeRoom* Actor = World->SpawnActor<AMRRuntimeRoom>();
	Actor->SetRoomFile(MovableName);
	Actor->Build(Movable, {}, {}, nullptr);
	const FMRRooSector Was = Movable.Sectors[Index];
	const int32 Rebuilds = Actor->GetRebuilds();
	Actor->MoveSector(MRMsg::ANIMATE_FLOOR_LIFT, Was.ServerId, static_cast<int16>(Was.FloorHeight + 32), 0);
	TestEqual(TEXT("a lift at speed 0 moves at once"), static_cast<int32>(Actor->GetRoom().Sectors[Index].FloorHeight), Was.FloorHeight + 32);
	TestTrue(TEXT("and rebuilds the room"), Actor->GetRebuilds() > Rebuilds && !Actor->IsMoving());
	Actor->MoveSector(MRMsg::ANIMATE_FLOOR_LIFT, Was.ServerId, Was.FloorHeight, 64);
	TestTrue(TEXT("a lift at speed 64 moves over time"), Actor->IsMoving());
	Actor->Tick(0.25f);  // 32 units at 64 a second: half way
	TestEqual(TEXT("half way after a quarter second"), static_cast<int32>(Actor->GetRoom().Sectors[Index].FloorHeight), Was.FloorHeight + 16);
	Actor->Tick(0.5f);
	TestTrue(TEXT("and stops where it was told"), !Actor->IsMoving() && Actor->GetRoom().Sectors[Index].FloorHeight == Was.FloorHeight);
	Actor->ChangeSector(Was.ServerId, 2, MRMsg::CHANGE_OVERRIDE);
	TestEqual(TEXT("BP_SECTOR_CHANGE sets the depth"), Actor->GetRoom().Sectors[Index].Depth(), 2);
	Actor->ChangeSector(Was.ServerId, MRMsg::CHANGE_OVERRIDE, 3);
	TestTrue(TEXT("and the scroll speed, keeping the depth"),
		Actor->GetRoom().Sectors[Index].Depth() == 2 && ((Actor->GetRoom().Sectors[Index].Flags & 0x0C) >> 2) == 3);
	const bool bHave = Actor->ChangeTexture(Was.ServerId, 1234, MRMsg::CTF_FLOOR);
	TestTrue(TEXT("BP_CHANGE_TEXTURE changes the floor"), Actor->GetRoom().Sectors[Index].FloorTexture == 1234);
	TestFalse(TEXT("and says the texture is still to fetch"), bHave);
	Actor->ResetChanges();
	TestTrue(TEXT("a room entry starts from the file again"), Actor->GetRoom().Sectors[Index].FloorTexture == Was.FloorTexture
		&& Actor->GetRoom().Sectors[Index].Flags == Was.Flags);
	AddInfo(FString::Printf(TEXT("changes tried on %s, sector id %d"), *MovableName, Was.ServerId));
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRWorldSoundTest, "Meridian.World.Sound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRWorldSoundTest::RunTest(const FString& Parameters)
{
	// the server's .ogg sounds decode (MRServerSound::Decode): any the online test has cached
	TArray<FString> Found;
	IFileManager::Get().FindFilesRecursive(Found, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MRNet")), TEXT("*.ogg"), true, false);
	if (Found.Num() == 0)
	{
		AddWarning(TEXT("no cached .ogg (run tools/ue/run_net_test.ps1 once): skipped"));
		return true;
	}
	Found.Sort();
	int32 Decoded = 0;
	for (int32 i = 0; i < FMath::Min(Found.Num(), 10); ++i)
	{
		TArray<uint8> Bytes;
		FString Error;
		FFileHelper::LoadFileToArray(Bytes, *Found[i]);
		const FString Name = FPaths::GetCleanFilename(Found[i]);
		const TSharedPtr<FMRPcmSound> Pcm = MRServerSound::Decode(Bytes, Error);
		if (TestTrue(FString::Printf(TEXT("%s decodes (%s)"), *Name, *Error), Pcm.IsValid()))
		{
			TestTrue(FString::Printf(TEXT("%s has samples at a rate"), *Name),
				Pcm->Samples.Num() > 0 && Pcm->SampleRate >= 8000 && Pcm->Channels >= 1 && Pcm->DurationSeconds() > 0.f);
			++Decoded;
		}
	}
	const TArray<uint8> Junk = {'n', 'o', 't', ' ', 'o', 'g', 'g'};
	FString Error;
	TestFalse(TEXT("junk isn't a sound"), MRServerSound::Decode(Junk, Error).IsValid());
	AddInfo(FString::Printf(TEXT("%d sounds decoded"), Decoded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRWorldBgfTest, "Meridian.World.Bgf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRWorldBgfTest::RunTest(const FString& Parameters)
{
	// BGF decoding against tools/bgf2png's catalog: sizes (transposed), shrink, transparency
	const TSharedPtr<FJsonObject> Cat = LoadJson(FPaths::Combine(RepoDir(), TEXT("build"), TEXT("textures"), TEXT("catalog.json")));
	const FString Res = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")), TEXT("Meridian-104"), TEXT("resource"));
	if (!Cat.IsValid() || !IFileManager::Get().DirectoryExists(*Res))
	{
		AddWarning(TEXT("no texture catalog or client resources: skipped"));
		return true;
	}
	TestEqual(TEXT("the palette has 256 colours"), FMRBgf::Palette().Num(), 256);
	int32 Checked = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Cat->GetObjectField(TEXT("textures"))->Values)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Res, Pair.Key + TEXT(".bgf"))))
		{
			continue;
		}
		const TSharedPtr<FJsonObject> T = Pair.Value->AsObject();
		FMRBgf Bgf;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("%s decodes (%s)"), *Pair.Key, *Error), Bgf.Load(Bytes, Error)))
		{
			continue;
		}
		const FIntPoint Size = Bgf.TextureSize();
		TestEqual(FString::Printf(TEXT("%s width"), *Pair.Key), Size.X, static_cast<int32>(T->GetNumberField(TEXT("w"))));
		TestEqual(FString::Printf(TEXT("%s height"), *Pair.Key), Size.Y, static_cast<int32>(T->GetNumberField(TEXT("h"))));
		TestEqual(FString::Printf(TEXT("%s shrink"), *Pair.Key), Bgf.Shrink, static_cast<int32>(T->GetNumberField(TEXT("shrink"))));
		TestEqual(FString::Printf(TEXT("%s transparency"), *Pair.Key), Bgf.HasTransparency(), T->GetBoolField(TEXT("has_transparency")));
		++Checked;
	}
	TestTrue(TEXT("checked some textures"), Checked > 0);
	AddInfo(FString::Printf(TEXT("%d textures checked"), Checked));
	return true;
}

#endif

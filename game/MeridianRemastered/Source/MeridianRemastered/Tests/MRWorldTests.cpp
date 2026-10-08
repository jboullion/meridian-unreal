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

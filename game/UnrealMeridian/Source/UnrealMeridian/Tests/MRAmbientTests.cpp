// Automation tests for ambient life (docs/adr/0013): Shards' nav grids, the looks, the speech and the crowd.
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian.Ambient;Quit" -unattended -nullrhi

#include "Ambient/MRAmbientData.h"
#include "Ambient/MRAmbientLife.h"
#include "Ambient/MRCritters.h"
#include "Ambient/MRNavGrid.h"
#include "Net/MRSocialClient.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Zones/MRZoneSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MRAmbient;

namespace
{
	bool LoadNav(const TCHAR* Room, FMRNavGrid& Out)
	{
		TArray<uint8> Bytes;
		return FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ambient"), TEXT("nav"), Room)) && Out.Decode(Bytes);
	}

	FMRAmbientLife::FExits LoadExits(const FString& Room)
	{
		FMRAmbientLife::FExits Out;
		FString Text;
		TSharedPtr<FJsonObject> Root;
		const TSharedPtr<FJsonObject>* Rooms = nullptr;
		const TSharedPtr<FJsonObject>* R = nullptr;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ambient"), TEXT("exits.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root->TryGetObjectField(TEXT("rooms"), Rooms)
			|| !(*Rooms)->TryGetObjectField(Room, R))
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& V : (*R)->GetArrayField(TEXT("doors")))
		{
			const TArray<TSharedPtr<FJsonValue>>& D = V->AsArray();
			FMRAmbientLife::FExits::FDoor Door;
			Door.Row = int32(D[0]->AsNumber());
			Door.Col = int32(D[1]->AsNumber());
			if (D.Num() > 2 && D[2]->Type == EJson::String)
			{
				Door.To = D[2]->AsString();
			}
			Out.Doors.Add(Door);
		}
		return Out;
	}

	/** Run a crowd for Seconds with the player standing at a cell; the most seen at once */
	int32 RunCrowd(FMRAmbientLife& Life, double Seconds, int32& OutWalking)
	{
		FMRAmbientLife::FFrame F;
		const int32 Cell = Life.GetGrid().RandomCell([]() { return 0.5; }, 1);
		F.bHasPlayer = Cell >= 0;
		F.Player = Cell >= 0 ? Life.GetGrid().CellCentre(Cell) : FVector2D::ZeroVector;
		F.Facing = 0;
		F.Hour = 13.0;
		int32 Most = 0;
		OutWalking = 0;
		for (double T = 0.0; T < Seconds * 1000.0; T += 50.0)
		{
			Life.Update(50.0, F);
			Most = FMath::Max(Most, Life.GetWalkers().Num());
			for (const FMRAmbientLife::FWalker& W : Life.GetWalkers())
			{
				OutWalking += W.bWalking ? 1 : 0;
			}
		}
		return Most;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRAmbientNavTest, "Meridian.Ambient.NavGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRAmbientNavTest::RunTest(const FString& Parameters)
{
	FMRNavGrid G;
	if (!TestTrue(TEXT("raza.nav reads (tools/world/ambient_data.py)"), LoadNav(TEXT("raza.nav"), G)))
	{
		return false;
	}
	TestTrue(TEXT("a grid of half squares"), G.Cols > 50 && G.Rows > 30);
	const TArray<int32> Sizes = G.AreaSizes();
	TestTrue(TEXT("the largest area first"), Sizes.Num() > 1 && Sizes[1] > 500);
	FRand Rand(7);
	int32 Found = 0;
	for (int32 K = 0; K < 20; ++K)
	{
		const int32 A = G.RandomCell([&Rand]() { return Rand(); }, 1), B = G.RandomCell([&Rand]() { return Rand(); }, 1);
		TArray<FVector2D> Path;
		if (A < 0 || B < 0 || !G.FindPath(G.CellCentre(A), G.CellCentre(B), Path))
		{
			continue;
		}
		++Found;
		FVector2D From = G.CellCentre(A);
		for (const FVector2D& P : Path)
		{
			TestTrue(TEXT("each leg of a pulled path is clear"), G.LineClear(From, P));
			From = P;
		}
	}
	TestTrue(TEXT("paths across the main area are found"), Found >= 18);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRAmbientLooksTest, "Meridian.Ambient.Looks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRAmbientLooksTest::RunTest(const FString& Parameters)
{
	FRand Rand(11);
	for (int32 K = 0; K < 40; ++K)
	{
		const FLook Looks[] = {RandomLook(Rand), GuardLook(Rand, 9), KnightLook(Rand, TEXT("duke")), LadyLook(Rand), NobleLook(Rand), FighterLook(Rand), MageLook(Rand)};
		for (const FLook& L : Looks)
		{
			FMRSpriteAppearance A;
			TestTrue(TEXT("a look is a player figure"), ToAppearance(L, K % 2 == 0, A));
			TestFalse(TEXT("its torso is drawn"), A.BodyBgf.IsNone());
			TestFalse(TEXT("its legs are drawn"), A.LegsBgf.IsNone());
			TestFalse(TEXT("its head is drawn"), A.HeadBgf.IsNone());
			if (!L.Hat.IsEmpty())
			{
				TestTrue(TEXT("a hat is an item, not the hair"), A.Overlays.ContainsByPredicate([&L](const FMRSpriteOverlay& O) { return O.Bgf == FName(*L.Hat); }));
			}
		}
	}
	TestEqual(TEXT("EncodeTwoColorXLAT red on skin 1"), TwoColorXlat(0, 1), 0x88);
	TestEqual(TEXT("dark green"), TwoColorXlat(11, 2), 123);
	TestTrue(TEXT("plate is fine armour"), IsImpressiveBody(TEXT("bte.bgf")));
	TestFalse(TEXT("a shirt isn't"), IsImpressiveBody(TEXT("bta")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRAmbientTalkTest, "Meridian.Ambient.Chatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRAmbientTalkTest::RunTest(const FString& Parameters)
{
	const FData& D = FData::Get();
	TestTrue(TEXT("ambient.json loads"), D.IsLoaded() && D.Room(TEXT("razainn.roo")) != nullptr);
	TestEqual(TEXT("Raza's street"), int32(D.Room(TEXT("raza.roo"))->Kind), int32(EKind::Town));
	TestTrue(TEXT("a smithy is an errand"), D.IsErrandRoom(TEXT("razasmith.roo")));
	TestFalse(TEXT("the Inn isn't"), D.IsErrandRoom(TEXT("razainn.roo")));
	TestEqual(TEXT("the streets empty at night"), D.BusyAt(EKind::Town, 2.0), 0.3, 1e-6);
	FRand Rand(3);
	for (ERole Role : {ERole::Folk, ERole::Patron, ERole::Customer, ERole::Guard, ERole::Knight, ERole::Lady, ERole::Noble})
	{
		FChatter C;
		C.Role = Role;
		C.Place = TEXT("raza");
		C.Hour = 13.0;
		TestFalse(TEXT("everyone has something to say"), D.ChatterLine(Rand, C).IsEmpty() || D.ChatterLine(Rand, C) == TEXT("..."));
		TestTrue(TEXT("a name"), D.PersonName(Rand, Role, false).Contains(TEXT(" ")));
	}
	FChatter Fan;
	Fan.FanOf = TEXT("Bram the Bold");
	TestTrue(TEXT("a fan names their fighter"), D.ChatterLine(Rand, Fan).Contains(TEXT("Bram")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRAmbientCrowdTest, "Meridian.Ambient.Crowd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRAmbientCrowdTest::RunTest(const FString& Parameters)
{
	const FData& D = FData::Get();
	FMRNavGrid Inn, Street;
	if (!TestTrue(TEXT("the Inn's and Raza's grids"), LoadNav(TEXT("razainn.nav"), Inn) && LoadNav(TEXT("raza.nav"), Street)))
	{
		return false;
	}
	int32 Walking = 0;
	FMRAmbientLife Patrons(*D.Room(TEXT("razainn.roo")), LoadExits(TEXT("razainn.roo")), Inn, 1, 0x40000000u);
	const int32 InInn = RunCrowd(Patrons, 60.0, Walking);
	TestTrue(FString::Printf(TEXT("the Inn has patrons by day (%d)"), InInn), InInn >= 2 && InInn <= 8);
	FMRAmbientLife Town(*D.Room(TEXT("raza.roo")), LoadExits(TEXT("raza.roo")), Street, 2, 0x40010000u);
	const int32 InTown = RunCrowd(Town, 90.0, Walking);
	TestTrue(FString::Printf(TEXT("Raza's street has walkers (%d) and they walk"), InTown), InTown >= 3 && Walking > 100);
	TestTrue(TEXT("a guard on a beat"), Town.GetWalkers().ContainsByPredicate([](const FMRAmbientLife::FWalker& W) { return W.Role == ERole::Guard && W.bHasBeat; }));
	for (const FMRAmbientLife::FWalker& W : Town.GetWalkers())
	{
		FMRAmbientLife::FPersonInfo Info;
		TestTrue(TEXT("everyone can be inspected"), Town.Describe(W.Id, Info) && !Info.Name.IsEmpty() && !Info.Doing.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRAmbientCritterTest, "Meridian.Ambient.Critters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRAmbientCritterTest::RunTest(const FString& Parameters)
{
	// critters.ts habitatOf, by the room's terrain
	FMRCritters::FHabitat H;
	TestTrue(TEXT("Raza's street: rats"), FMRCritters::HabitatOf(TEXT("raza.roo"), {TEXT("CITY"), TEXT("LAKE"), TEXT("ROAD"), TEXT("FOREST")}, H) && H.Name == TEXT("street"));
	TestTrue(TEXT("the forest: the wild"), FMRCritters::HabitatOf(TEXT("c6.roo"), {TEXT("FOREST")}, H) && H.Name == TEXT("wild") && H.Mix.Num() == 4);
	TestTrue(TEXT("a sewer: rats that swim"), FMRCritters::HabitatOf(TEXT("sewer.roo"), {TEXT("SEWERS")}, H) && H.bWades);
	TestTrue(TEXT("the Inn: an indoor rat now and then"), FMRCritters::HabitatOf(TEXT("razainn.roo"), {TEXT("CITY"), TEXT("SHOP")}, H) && H.Count < 1.0);
	TestTrue(TEXT("Raza's vault: rats"), FMRCritters::HabitatOf(TEXT("razavault.roo"), {}, H) && H.Name == TEXT("vault"));
	TestFalse(TEXT("the arena: none"), FMRCritters::HabitatOf(TEXT("tosarena.roo"), {}, H));
	TestTrue(TEXT("a desert: scorpions and ants"), FMRCritters::HabitatOf(TEXT("desert1.roo"), {TEXT("DESERT")}, H) && H.Name == TEXT("desert"));
	TestEqual(TEXT("a rat's death sound"), FString(FMRCritters::Species(0).Sound), FString(TEXT("rat_dth.ogg")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRAmbientRelayTest, "Meridian.Ambient.Relay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMRAmbientRelayTest::RunTest(const FString& Parameters)
{
	// the relay's hello (Shards socialProtocol.ts): a P-256 key, the text signed as WebCrypto signs it
	TArray<uint8> Der, Public, Again;
	TestTrue(TEXT("a new key"), FMRSocialClient::EnsureKey(Der, Public) && Der.Num() > 0);
	TestEqual(TEXT("its raw public point, uncompressed"), Public.Num() > 0 ? int32(Public[0]) : -1, 4);
	TestTrue(TEXT("the same key read back"), FMRSocialClient::EnsureKey(Der, Again) && Again == Public);
	const FString Text = FMRSocialClient::HelloText(TEXT("abc123"), TEXT("ws://localhost:5173/ws"), TEXT("Unrealtest"));
	TestEqual(TEXT("the hello's text"), Text, FString(TEXT("meridian-shards-social\nabc123\nws://localhost:5173/ws\nUnrealtest")));
	TArray<uint8> Sig;
	TestTrue(TEXT("signed"), FMRSocialClient::Sign(Der, Text, Sig) && Sig.Num() == 64);
	TestTrue(TEXT("and it checks out"), FMRSocialClient::Verify(Public, Text, Sig));
	TestFalse(TEXT("not for another text"), FMRSocialClient::Verify(Public, Text + TEXT("x"), Sig));
	TestFalse(TEXT("base64url, no padding"), FMRSocialClient::Base64Url(Public).Contains(TEXT("=")) || FMRSocialClient::Base64Url(Public).Contains(TEXT("+")));
	return true;
}

#endif

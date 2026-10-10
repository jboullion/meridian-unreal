// Automation tests for character creation (docs/research/blakserv-protocol.md "Character creation")
// and players' looks from the server's overlays (docs/sprites.md "Players online").
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian;Quit" -unattended -nullrhi

#include "Algo/AllOf.h"
#include "Character/MRSpriteData.h"
#include "Misc/AutomationTest.h"
#include "Net/MRCharInfo.h"
#include "Net/MRNetLook.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRProtocol.h"
#include "Net/MRResources.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** A BP_CHARINFO body as system.kod SendCharInfo builds it (two of everything, mostly). */
	TArray<uint8> CharInfoBody()
	{
		FMRWriter W(MRMsg::BP_CHARINFO);
		W.U8(2).U8(0).U8(0x2F);                              // hair colours
		W.U8(2).U8(1).U8(2);                                 // skins
		W.U32(2).U32(101).U32(102).U32(103);                 // male: hair, head
		W.U32(1).U32(104).U32(1).U32(105).U32(1).U32(106);   // eyes, noses, mouths
		W.U32(1).U32(102).U32(107);                          // female: hair (bald), head
		W.U32(1).U32(108).U32(1).U32(109).U32(1).U32(110);
		W.U32(2);                                            // spells: number, name, description, cost, school
		W.U32(3).U32(201).U32(202).U32(10).U8(4);
		W.U32(9).U32(203).U32(204).U32(25).U8(1);
		W.U32(1);                                            // skills
		W.U32(5).U32(205).U32(206).U32(10).U8(10);
		return W.Bytes;
	}

	void Names(FMRResourceTable& R)
	{
		const TPair<uint32, const TCHAR*> All[] = {{101, TEXT("ptcd.bgf")}, {102, TEXT("blank.bgf")}, {103, TEXT("phax.bgf")},
			{104, TEXT("peax.bgf")}, {105, TEXT("pnax.bgf")}, {106, TEXT("pmax.bgf")}, {107, TEXT("phkx.bgf")}, {108, TEXT("pekx.bgf")},
			{109, TEXT("pnkx.bgf")}, {110, TEXT("pmkx.bgf")}, {201, TEXT("fog")}, {202, TEXT("~BSummons~n a wall of fog.")},
			{203, TEXT("holy touch")}, {204, TEXT("Blesses a weapon.")}, {205, TEXT("punch")}, {206, TEXT("Hit things.")}};
		for (const TPair<uint32, const TCHAR*>& P : All)
		{
			R.SetDynamic(P.Key, P.Value);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRNetCharInfoTest, "Meridian.Net.CharInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRNetCharInfoTest::RunTest(const FString& Parameters)
{
	FMRResourceTable Res;
	Names(Res);
	TArray<uint8> Body = CharInfoBody();
	FMRCharInfo Info;
	{
		FMRReader R(Body);
		TestTrue(TEXT("parses"), MRCharInfo::Parse(R, Res, Info) && Info.IsValid());
	}
	TestEqual(TEXT("hair colours"), Info.HairXlats.Num(), 2);
	TestEqual(TEXT("skins"), Info.SkinXlats.Num(), 2);
	TestEqual(TEXT("male hair"), Info.Male.Hair.Num(), 2);
	TestEqual(TEXT("male head"), Info.Male.Head.Bgf, FString(TEXT("phax")));
	TestEqual(TEXT("female hair is bald"), Info.Female.Hair[0].Bgf, FString(TEXT("blank")));
	TestEqual(TEXT("female eyes"), Info.Female.Eyes[0].Bgf, FString(TEXT("pekx")));
	TestEqual(TEXT("spells"), Info.Spells.Num(), 2);
	TestEqual(TEXT("spell name"), Info.Spells[0].Name, FString(TEXT("fog")));
	TestEqual(TEXT("description without style codes"), Info.Spells[0].Desc, FString(TEXT("Summons a wall of fog.")));
	TestEqual(TEXT("a 25-point spell is level 2"), Info.Spells[1].Level(), 2);
	TestEqual(TEXT("skill school"), static_cast<int32>(Info.Skills[0].School), 10);

	// the original client refuses a BP_CHARINFO with bytes left over (char.c)
	Body.Add(0);
	{
		FMRReader R(Body);
		FMRCharInfo Junk;
		TestFalse(TEXT("left-over bytes"), MRCharInfo::Parse(R, Res, Junk));
	}
	Body.SetNum(Body.Num() - 8);
	{
		FMRReader R(Body);
		FMRCharInfo Junk;
		TestFalse(TEXT("cut short"), MRCharInfo::Parse(R, Res, Junk));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRNetNewCharInfoTest, "Meridian.Net.NewCharInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRNetNewCharInfoTest::RunTest(const FString& Parameters)
{
	FMRResourceTable Res;
	Names(Res);
	FMRCharInfo Info;
	TArray<uint8> Body = CharInfoBody();
	FMRReader In(Body);
	MRCharInfo::Parse(In, Res, Info);

	FMRNewCharacter C;
	C.SlotId = 0x1234;
	C.Name = TEXT("  Aldera  ");
	C.Description = TEXT("Hi");
	C.bFemale = true;
	C.HairColour = 1;
	C.Skin = 1;
	const int32 Mage[] = {40, 50, 45, 15, 45, 25};
	FMemory::Memcpy(C.Stats, Mage, sizeof(Mage));
	C.Spells = {3};
	C.Skills = {5};
	FString Why;
	TestEqual(TEXT("valid"), static_cast<int32>(MRCharInfo::Validate(C, Info, Why)), static_cast<int32>(MRCharInfo::EProblem::None));

	// BP_SYSTEM, BP_NEW_CHARINFO, then sprocket.c's fields in order
	const TArray<uint8> Bytes = MRCharInfo::Write(C, Info).Bytes;
	FMRReader R(Bytes, 0);
	TestEqual(TEXT("BP_SYSTEM"), static_cast<int32>(R.U8()), static_cast<int32>(MRMsg::BP_SYSTEM));
	TestEqual(TEXT("BP_NEW_CHARINFO"), static_cast<int32>(R.U8()), static_cast<int32>(MRMsg::BP_NEW_CHARINFO));
	TestEqual(TEXT("slot"), R.U32(), 0x1234u);
	TestEqual(TEXT("name, trimmed"), R.Str(), FString(TEXT("Aldera")));
	TestEqual(TEXT("description"), R.Str(), FString(TEXT("Hi")));
	TestEqual(TEXT("female = 2"), static_cast<int32>(R.U8()), 2);
	TestEqual(TEXT("five face parts"), static_cast<int32>(R.U16()), 5);
	TestEqual(TEXT("head"), R.U32(), 107u);
	TestEqual(TEXT("hair"), R.U32(), 102u);
	TestEqual(TEXT("eyes"), R.U32(), 108u);
	TestEqual(TEXT("nose"), R.U32(), 109u);
	TestEqual(TEXT("mouth"), R.U32(), 110u);
	TestEqual(TEXT("hair translation, not its index"), static_cast<int32>(R.U8()), 0x2F);
	TestEqual(TEXT("skin translation"), static_cast<int32>(R.U8()), 2);
	TestEqual(TEXT("six stats"), static_cast<int32>(R.U16()), 6);
	for (int32 i = 0; i < 6; ++i)
	{
		TestEqual(FString::Printf(TEXT("stat %d"), i), R.I32(), Mage[i]);
	}
	TestEqual(TEXT("one spell"), static_cast<int32>(R.U16()), 1);
	TestEqual(TEXT("spell number"), R.U32(), 3u);
	TestEqual(TEXT("one skill"), static_cast<int32>(R.U16()), 1);
	TestEqual(TEXT("skill number"), R.U32(), 5u);
	TestTrue(TEXT("nothing after"), R.IsOk() && R.AtEnd());

	// what the server would refuse or quietly replace
	auto Problem = [&](TFunction<void(FMRNewCharacter&)> Change)
	{
		FMRNewCharacter X = C;
		Change(X);
		FString W;
		return MRCharInfo::Validate(X, Info, W);
	};
	using EP = MRCharInfo::EProblem;
	TestEqual(TEXT("short name"), Problem([](FMRNewCharacter& X) { X.Name = TEXT("Al"); }), EP::Name);
	TestEqual(TEXT("illegal character"), Problem([](FMRNewCharacter& X) { X.Name = TEXT("Bob#1"); }), EP::Name);
	TestEqual(TEXT("accented letters are fine"), Problem([](FMRNewCharacter& X) { X.Name = TEXT("Ünal the 2nd"); }), EP::None);
	TestEqual(TEXT("a stat over 50"), Problem([](FMRNewCharacter& X) { X.Stats[0] = 51; X.Stats[1] = 39; }), EP::Stats);
	TestEqual(TEXT("over 220 points"), Problem([](FMRNewCharacter& X) { X.Stats[3] = 16; }), EP::Stats);
	TestEqual(TEXT("over 45 spell points"), Problem([](FMRNewCharacter& X) { X.Spells = {3, 9}; X.Skills = {5}; X.Spells.Add(9); }), EP::Abilities);
	TestEqual(TEXT("left points are fine"), Problem([](FMRNewCharacter& X) { X.Stats[0] = 30; X.Spells.Reset(); }), EP::None);
	TestEqual(TEXT("points left"), MRCharInfo::AbilityPointsLeft(C, Info), 25);

	// switching gender wraps the part indexes into the other list (charface.c)
	FMRNewCharacter G = C;
	G.bFemale = false;
	G.Hair = 3;
	MRCharInfo::ClampParts(G, Info);
	TestEqual(TEXT("hair wrapped"), G.Hair, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSpriteOverlaysTest, "Meridian.Sprites.Overlays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRSpriteOverlaysTest::RunTest(const FString& Parameters)
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	if (!Lib.Looks.Contains(TEXT("player_female")) || !Lib.Atlases.Contains(TEXT("pelx")))
	{
		AddError(TEXT("player_parts.json has no player_female or creator parts: run tools/sprites/build_player_sprites.py"));
		return false;
	}
	// a female player as player.kod SendOverlays describes her
	FMRNetObject O;
	O.Flags = MRMsg::OF_PLAYER;
	O.Icon = TEXT("btb.bgf");
	O.Xlat = FMRSpriteColours::ClothesXlat(3, 1);
	O.OverlayParts = {
		{TEXT("blb"), 31, O.Xlat}, {TEXT("brb"), 21, O.Xlat}, {TEXT("bfb"), 41, FMRSpriteColours::ClothesXlat(6, 1)},
		{TEXT("phkx"), 1, 2}, {TEXT("pmlx"), 12, 2}, {TEXT("pelx"), 11, 2}, {TEXT("pnlx"), 14, 2}, {TEXT("ptdr"), 13, 0x2C}};
	FMRSpriteAppearance A;
	TestTrue(TEXT("a player"), MRNetLook::AppearanceFromObject(O, A));
	TestEqual(TEXT("female base"), A.Look, FName(TEXT("player_female")));
	TestEqual(TEXT("eyes"), A.EyesBgf, FName(TEXT("pelx")));
	TestEqual(TEXT("mouth"), A.MouthBgf, FName(TEXT("pmlx")));
	TestEqual(TEXT("hair"), A.HairBgf, FName(TEXT("ptdr")));
	TestEqual(TEXT("skin from the face's translation"), A.Skin, 1);
	TestEqual(TEXT("hair colour from its translation (black)"), A.Hair, 12);
	TestEqual(TEXT("shirt"), A.Shirt, 3);
	TestEqual(TEXT("pants"), A.Pants, 6);

	// bald, a hat (no hair at all), a part we haven't converted
	O.OverlayParts[7].Bgf = TEXT("blank");
	MRNetLook::AppearanceFromObject(O, A);
	TestEqual(TEXT("bald"), A.HairBgf, FName(TEXT("blank")));
	O.OverlayParts.RemoveAt(7);
	O.OverlayParts[5].Bgf = TEXT("zzzz");
	MRNetLook::AppearanceFromObject(O, A);
	TestEqual(TEXT("no hair overlay: none drawn"), A.HairBgf, FName(TEXT("blank")));
	TestTrue(TEXT("unconverted eyes: the look's own"), A.EyesBgf.IsNone());
	// not a player, but with a player's body overlays (a soldier, a logged-off ghost): drawn as one
	O.Flags = 0;
	TestTrue(TEXT("not a player, with a player's overlays: a player figure"), MRNetLook::AppearanceFromObject(O, A));
	O.OverlayParts.Reset();
	TestFalse(TEXT("not a player, without them"), MRNetLook::AppearanceFromObject(O, A));

	// a look with a part replaced places it on the same hotspot
	FMRSpriteLook L = Lib.Looks[TEXT("player_female")];
	FMRSpritePart* Eyes = L.Parts.FindByPredicate([](const FMRSpritePart& P) { return P.Name == TEXT("eyes"); });
	TestNotNull(TEXT("the look has eyes"), Eyes);
	if (Eyes)
	{
		Eyes->Bgf = Eyes->Atlas = TEXT("pelx");
		TArray<FMRSpritePlaced> Placed;
		FVector2f Feet;
		int32 Shrink = 0;
		TestTrue(TEXT("placed"), Lib.Place(L, {}, 0, Placed, Feet, Shrink));
		const FMRSpritePlaced* P = Placed.FindByPredicate([](const FMRSpritePlaced& X) { return X.Part == TEXT("eyes"); });
		TestTrue(TEXT("the new eyes are drawn"), P && P->PartDef && P->PartDef->Bgf == TEXT("pelx") && P->Bitmap != INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSpriteEquipmentTest, "Meridian.Sprites.Equipment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRSpriteEquipmentTest::RunTest(const FString& Parameters)
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	if (!Lib.Equipment.Contains(TEXT("bte")) || !Lib.Equipment.Contains(TEXT("metlshld")) || !Lib.Atlases.Contains(TEXT("helm")))
	{
		AddError(TEXT("player_parts.json has no equipment: run tools/kod_extract/extract.py, then tools/sprites/build_player_sprites.py"));
		return false;
	}
	auto Ov = [](const TCHAR* Bgf, uint8 Hotspot, int32 Xlat, int32 Group)
	{
		FMRNetOverlay V{Bgf, Hotspot, Xlat};
		V.Animation.Type = MRMsg::ANIMATE_NONE;
		V.Animation.Group = static_cast<uint16>(Group);
		return V;
	};
	// a man in plate armour with a sword, a shield and a helmet, as player.kod SendOverlays sends him
	FMRNetObject O;
	O.Flags = MRMsg::OF_PLAYER;
	O.Icon = TEXT("bte.bgf");
	O.Xlat = 0;
	O.OverlayParts = {Ov(TEXT("bla"), 31, 5, 7), Ov(TEXT("bra"), 21, 5, 17), Ov(TEXT("bfa"), 41, 9, 1), Ov(TEXT("phax"), 1, 2, 1),
		Ov(TEXT("pmax"), 12, 2, 1), Ov(TEXT("peax"), 11, 2, 1), Ov(TEXT("pnax"), 14, 2, 1), Ov(TEXT("ptcd"), 13, 0x2C, 1),
		Ov(TEXT("swordov"), 22, 0, 4), Ov(TEXT("metlshld"), 32, 0, 2), Ov(TEXT("helm"), 13, 0, 1)};
	TestEqual(TEXT("the items come after the hair"), MRNetLook::FirstItemOverlay(O), 8);
	FMRSpriteAppearance A;
	TestTrue(TEXT("a player"), MRNetLook::AppearanceFromObject(O, A));
	TestEqual(TEXT("male base"), A.Look, FName(TEXT("player_male")));
	TestEqual(TEXT("the armour's torso"), A.BodyBgf, FName(TEXT("bte")));
	TestEqual(TEXT("its translation, the server's"), A.BodyXlat, 0);
	TestEqual(TEXT("the arms' translation"), A.ArmsXlat, 5);
	TestEqual(TEXT("the legs' translation"), A.LegsXlat, 9);
	TestEqual(TEXT("hair kept under the helmet"), A.HairBgf, FName(TEXT("ptcd")));
	TestEqual(TEXT("three item overlays"), A.Overlays.Num(), 3);
	if (A.Overlays.Num() == 3)
	{
		TestTrue(TEXT("the sword in the right hand on group 4"), A.Overlays[0].Bgf == TEXT("swordov") && A.Overlays[0].Hotspot == 22 && A.Overlays[0].Group == 4);
		TestTrue(TEXT("the shield in the left on group 2"), A.Overlays[1].Bgf == TEXT("metlshld") && A.Overlays[1].Hotspot == 32 && A.Overlays[1].Group == 2);
		TestTrue(TEXT("the helmet on the hair's hotspot"), A.Overlays[2].Bgf == TEXT("helm") && A.Overlays[2].Hotspot == 13);
	}
	// a helmet that takes the hair off (helm.kod RemoveHair): the hat comes right after the nose
	O.OverlayParts.RemoveAt(7);
	TestEqual(TEXT("no hair: the items come after the nose"), MRNetLook::FirstItemOverlay(O), 7);
	MRNetLook::AppearanceFromObject(O, A);
	TestEqual(TEXT("no hair drawn"), A.HairBgf, FName(TEXT("blank")));
	TestEqual(TEXT("still three items"), A.Overlays.Num(), 3);

	// the bent left arm (group 7) carries the shield's hotspot, the right (17) the weapon's, from every side
	FMRSpriteLook L = Lib.Looks[TEXT("player_male")];
	for (const FMRSpriteOverlay& E : A.Overlays)
	{
		FMRSpritePart& P = L.Parts.AddDefaulted_GetRef();
		P.Name = E.Hotspot == 22 ? FName(TEXT("weapon")) : E.Hotspot == 32 ? FName(TEXT("shield")) : FName(TEXT("helmet"));
		P.Bgf = P.Atlas = E.Bgf.ToString();
		P.Hotspot = E.Hotspot;
	}
	const TMap<FName, int32> Groups = {{TEXT("left_arm"), 6}, {TEXT("right_arm"), 16}, {TEXT("weapon"), 3}, {TEXT("shield"), 1}, {TEXT("helmet"), 0}};
	for (int32 Angle = 0; Angle < 4096; Angle += 512)
	{
		TArray<FMRSpritePlaced> Placed;
		FVector2f Feet;
		int32 Shrink = 0;
		TestTrue(FString::Printf(TEXT("placed at %d"), Angle), Lib.Place(L, Groups, Angle, Placed, Feet, Shrink));
		for (const TCHAR* Part : {TEXT("weapon"), TEXT("shield"), TEXT("helmet")})
		{
			TestTrue(FString::Printf(TEXT("%s drawn at angle %d"), Part, Angle),
				Placed.ContainsByPredicate([Part](const FMRSpritePlaced& X) { return X.Part == Part; }));
		}
	}
	// a long blade swung wide needs a bigger render box, only while it's worn
	const FBox2f* Wide = Lib.Equipment[TEXT("spirswordov")].Bounds.Find(TEXT("male"));
	TestTrue(TEXT("a long sword grows the male box"), Wide && Wide->GetSize().X > Lib.Looks[TEXT("player_male")].Bounds.GetSize().X);
	TestEqual(TEXT("the looks know their gender"), Lib.Looks[TEXT("player_female")].Gender, FName(TEXT("female")));
	TestTrue(TEXT("the sword has a first-person picture"), Lib.FirstPerson.Contains(TEXT("swordov")));
	TestTrue(TEXT("every weapon's first-person picture is converted"), Algo::AllOf(Lib.FirstPerson, [&Lib](const TPair<FString, FMRFirstPersonOverlay>& F)
	{
		return Lib.Atlases.Contains(F.Value.Bgf);
	}));
	return true;
}

#endif

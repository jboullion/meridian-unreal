// Automation tests for sprite players (docs/sprites.md): the ports of the original client's angle
// choice, overlay placement and animation, checked against tools/sprites/m59sprites.py.
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian.Sprites;Quit" -unattended -nullrhi

#include "Character/MRSpriteData.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSpriteAngleTest, "Meridian.Sprites.Angles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRSpriteAngleTest::RunTest(const FString& Parameters)
{
	// GetObjectPdib: 8 views centred on multiples of 512
	for (int32 i = 0; i < 8; ++i)
	{
		TestEqual(*FString::Printf(TEXT("slot %d at its centre"), i), FMRSpriteLibrary::ViewSlot(i * 512, 8), i);
		TestEqual(*FString::Printf(TEXT("slot %d just before the next"), i), FMRSpriteLibrary::ViewSlot(i * 512 + 250, 8), i);
	}
	TestEqual(TEXT("wraps to the front"), FMRSpriteLibrary::ViewSlot(4095, 8), 0);
	TestEqual(TEXT("one view"), FMRSpriteLibrary::ViewSlot(2000, 1), 0);

	// facing the viewer = 0; UE yaw and the client's angle both turn clockwise
	TestEqual(TEXT("facing the viewer"), FMRSpriteLibrary::RelativeAngle(90.f, 90.f), 0);
	TestEqual(TEXT("back to the viewer"), FMRSpriteLibrary::RelativeAngle(270.f, 90.f), 2048);
	// facing +Y (east) seen from -X (south): its right side, the profile facing right (slot 6)
	TestEqual(TEXT("right side"), FMRSpriteLibrary::ViewSlot(FMRSpriteLibrary::RelativeAngle(90.f, 180.f), 8), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSpriteTrackTest, "Meridian.Sprites.Tracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRSpriteTrackTest::RunTest(const FString& Parameters)
{
	// the legs' walk: groups 2-5, 100 ms each, looping
	FMRSpriteTrackDef Walk;
	Walk.Mode = FMRSpriteTrackDef::EMode::Cycle;
	Walk.PeriodMs = 100;
	Walk.Low = 2;
	Walk.High = 5;
	FMRSpriteTrack T;
	T.Start(Walk);
	TestEqual(TEXT("starts on the low group"), T.Group, 2);
	T.Step(99.f);
	TestEqual(TEXT("holds for a period"), T.Group, 2);
	T.Step(1.f);
	TestEqual(TEXT("then steps"), T.Group, 3);
	T.Step(300.f);
	TestEqual(TEXT("wraps to the low group"), T.Group, 2);
	TestEqual(TEXT("next"), T.NextGroup(), 3);

	// a fist attack's right arm: 13 then 14 at 600 ms, then holds 1
	FMRSpriteTrackDef Once;
	Once.Mode = FMRSpriteTrackDef::EMode::Once;
	Once.PeriodMs = 600;
	Once.Low = 13;
	Once.High = 14;
	Once.Final = 1;
	T.Start(Once);
	T.Step(600.f);
	TestEqual(TEXT("second group"), T.Group, 14);
	TestTrue(TEXT("still playing"), T.IsPlaying());
	T.Step(600.f);
	TestEqual(TEXT("final group"), T.Group, 1);
	TestFalse(TEXT("finished"), T.IsPlaying());
	T.Step(5000.f);
	TestEqual(TEXT("stays on the final group"), T.Group, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSpritePlacementTest, "Meridian.Sprites.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRSpritePlacementTest::RunTest(const FString& Parameters)
{
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	if (!TestTrue(TEXT("data/sprites/player_parts.json loads"), Lib.IsValid()))
	{
		return false;
	}
	struct FExpect { const TCHAR* Part; int32 Bitmap; float X, Y, Scale; };
	auto Check = [&](FName LookName, const TMap<FName, int32>& Groups, int32 Angle, FVector2f Feet, TArray<FExpect> Expect)
	{
		const FMRSpriteLook* Look = Lib.Looks.Find(LookName);
		if (!TestNotNull(*FString::Printf(TEXT("look %s"), *LookName.ToString()), Look))
		{
			return;
		}
		TArray<FMRSpritePlaced> Placed;
		FVector2f OutFeet;
		int32 Shrink = 0;
		TestTrue(TEXT("placed"), Lib.Place(*Look, Groups, Angle, Placed, OutFeet, Shrink));
		TestEqual(TEXT("feet"), OutFeet, Feet);
		TestEqual(TEXT("torso shrink"), Shrink, 4);
		const FString Ctx = FString::Printf(TEXT("%s at %d"), *LookName.ToString(), Angle);
		if (!TestEqual(*(Ctx + TEXT(": bitmap count")), Placed.Num(), Expect.Num()))
		{
			return;
		}
		// draw order and positions as tools/sprites/m59sprites.py place()
		for (int32 i = 0; i < Expect.Num(); ++i)
		{
			const FMRSpritePlaced& P = Placed[i];
			const FString What = FString::Printf(TEXT("%s #%d %s"), *Ctx, i, Expect[i].Part);
			TestEqual(*(What + TEXT(" part")), P.Part, FName(Expect[i].Part));
			TestEqual(*(What + TEXT(" bitmap")), P.Bitmap, Expect[i].Bitmap);
			TestEqual(*(What + TEXT(" x")), P.Pos.X, Expect[i].X, 0.01f);
			TestEqual(*(What + TEXT(" y")), P.Pos.Y, Expect[i].Y, 0.01f);
			TestEqual(*(What + TEXT(" scale")), P.Scale, Expect[i].Scale, 0.001f);
		}
	};
	// standing, front: legs and head under the torso, face parts on the head, arms over it
	Check(TEXT("test_male"), {}, 0, FVector2f(25.5f, 183.f), {
		{TEXT("legs"), 0, 2.f, 50.f, 2.f}, {TEXT("head"), 0, 15.f, -27.f, 0.5714f},
		{TEXT("mouth"), 0, 17.286f, -12.714f, 0.2857f}, {TEXT("eyes"), 0, 17.286f, -19.571f, 0.2857f},
		{TEXT("nose"), 0, 23.f, -15.571f, 0.2857f}, {TEXT("hair"), 0, 20.714f, -31.f, 0.2857f},
		{TEXT("body"), 0, 0.f, 0.f, 1.f},
		{TEXT("left_arm"), 0, 43.f, 5.f, 1.f}, {TEXT("right_arm"), 0, -11.f, 5.f, 1.f}});
	// from behind: arms under, no face, legs over
	Check(TEXT("test_male"), {}, 1536, FVector2f(25.5f, 184.f), {
		{TEXT("left_arm"), 3, -15.f, 6.f, 1.f}, {TEXT("right_arm"), 3, 43.f, 6.f, 1.f},
		{TEXT("head"), 3, 15.f, -26.f, 0.5714f}, {TEXT("hair"), 3, 20.143f, -28.857f, 0.2857f},
		{TEXT("body"), 3, 0.f, 0.f, 1.f}, {TEXT("legs"), 3, 4.f, 52.f, 2.f}});
	// a sword in the hand (an overlay on the right arm), seen from behind
	Check(TEXT("test_sword"), {{TEXT("right_arm"), 16}, {TEXT("weapon"), 3}}, 2048, FVector2f(25.5f, 184.f), {
		{TEXT("weapon"), 21, 52.f, 66.f, 1.f},
		{TEXT("left_arm"), 3, -15.f, 6.f, 1.f}, {TEXT("right_arm"), 93, 39.f, 6.f, 1.f},
		{TEXT("head"), 3, 15.f, -26.f, 0.5714f}, {TEXT("hair"), 3, 14.429f, -27.714f, 0.2857f},
		{TEXT("body"), 3, 0.f, 0.f, 1.f}, {TEXT("legs"), 3, 4.f, 52.f, 2.f}});

	// a weapon attack seen from behind (torso group 2: bitmap 9 marks the right arm "over"): with
	// bBackArmsUnder the arm and its sword go under the torso, without it they are drawn on its back
	if (const FMRSpriteLook* Sword = Lib.Looks.Find(TEXT("test_sword")))
	{
		const TMap<FName, int32> Swing = {{TEXT("body"), 1}, {TEXT("right_arm"), 4}, {TEXT("left_arm"), 4}, {TEXT("weapon"), 1}};
		for (const bool bUnder : {true, false})
		{
			TArray<FMRSpritePlaced> Placed;
			FVector2f OutFeet;
			int32 Shrink = 0;
			TestTrue(TEXT("swing placed"), Lib.Place(*Sword, Swing, 2048, Placed, OutFeet, Shrink, nullptr, bUnder));
			auto IndexOf = [&Placed](const TCHAR* Part) { return Placed.IndexOfByPredicate([Part](const FMRSpritePlaced& P) { return P.Part == FName(Part); }); };
			const int32 Body = IndexOf(TEXT("body"));
			const FString Ctx = bUnder ? TEXT("back arms under") : TEXT("original layering");
			TestEqual(*(Ctx + TEXT(": right arm under the torso")), IndexOf(TEXT("right_arm")) < Body, bUnder);
			TestEqual(*(Ctx + TEXT(": sword under the torso")), IndexOf(TEXT("weapon")) < Body, bUnder);
		}
		// from the side the arm stays over the torso either way
		TArray<FMRSpritePlaced> Side;
		FVector2f OutFeet;
		int32 Shrink = 0;
		Lib.Place(*Sword, Swing, 3072, Side, OutFeet, Shrink);
		const int32 SideBody = Side.IndexOfByPredicate([](const FMRSpritePlaced& P) { return P.Part == TEXT("body"); });
		TestTrue(TEXT("side view: right arm over the torso"),
			Side.IndexOfByPredicate([](const FMRSpritePlaced& P) { return P.Part == TEXT("right_arm"); }) > SideBody);
	}

	// size: the original client's scale makes the male about 1.84 m
	TestEqual(TEXT("cm per torso pixel"), Lib.CmPerBasePixel(4), 0.859375f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSpriteTweenTest, "Meridian.Sprites.Tweens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRSpriteTweenTest::RunTest(const FString& Parameters)
{
	// tools/sprites/tweens.py: the walking legs (groups 2 -> 3, bitmaps 6 -> 12 from the front)
	// get in-betweens, appended after the bgf's own bitmaps
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	const FMRSpriteBgf* Legs = Lib.FindBgf(TEXT("bfa"));
	if (!TestNotNull(TEXT("bfa"), Legs))
	{
		return false;
	}
	if (Legs->Tweens.IsEmpty())
	{
		AddInfo(TEXT("no in-betweens in this build (data/sprites/upscale.json store.tweens 0): nothing to check"));
		return true;
	}
	const TArray<int32>* Tw = Legs->FindTweens(6, 12);
	if (!TestNotNull(TEXT("walk legs 6 > 12 have in-betweens"), Tw) || Tw->Num() == 0)
	{
		return false;
	}
	TestTrue(TEXT("after the 42 originals"), (*Tw)[0] >= 42 && Legs->Bitmaps.IsValidIndex(Tw->Last()));
	for (int32 I : *Tw)
	{
		TestTrue(TEXT("every in-between has an atlas cell"), Lib.Atlases.FindRef(TEXT("bfa")).Cells.Contains(I));
	}

	// an override puts the in-between where its own offset says, on the same hotspot as the original
	const FMRSpriteLook* Look = Lib.Looks.Find(TEXT("test_male"));
	TArray<FMRSpritePlaced> Placed;
	FVector2f Feet;
	int32 Shrink = 0;
	const TMap<FName, int32> Groups = {{TEXT("legs"), 1}};
	const TMap<FName, int32> Override = {{TEXT("legs"), (*Tw)[0]}};
	if (TestNotNull(TEXT("test_male"), Look) && TestTrue(TEXT("placed"), Lib.Place(*Look, Groups, 0, Placed, Feet, Shrink, &Override)))
	{
		const FMRSpritePlaced* L = Placed.FindByPredicate([](const FMRSpritePlaced& P) { return P.Part == FName(TEXT("legs")); });
		if (TestNotNull(TEXT("legs drawn"), L))
		{
			const FMRSpriteBitmap& Bm = Legs->Bitmaps[(*Tw)[0]];
			TestEqual(TEXT("the in-between"), L->Bitmap, (*Tw)[0]);
			TestEqual(TEXT("x = hotspot + its offset"), L->Pos.X, -166.f + Bm.XOff, 0.01f);
			TestEqual(TEXT("y = hotspot + its offset"), L->Pos.Y, -84.f + Bm.YOff, 0.01f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSpriteColourTest, "Meridian.Sprites.Colours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRSpriteColourTest::RunTest(const FString& Parameters)
{
	// EncodeTwoColorXLAT as tools/sprites/m59sprites.py two_color: 0x87 + colour * 11 + the skin's clothes ramp
	TestEqual(TEXT("grey shirt, skin3 (its clothes ramp is skin4's)"), FMRSpriteColours::ClothesXlat(9, 2), 237);
	TestEqual(TEXT("red shirt, skin1"), FMRSpriteColours::ClothesXlat(0, 0), 0x88);
	TestEqual(TEXT("decoded"), FMRSpriteColours::ClothesOf(237), 9);
	TestEqual(TEXT("not a clothes translation"), FMRSpriteColours::ClothesOf(0x2F), INDEX_NONE);
	TestEqual(TEXT("skin xlats are PT_BLUE_TO_SKIN1-4"), FMRSpriteColours::SkinXlat(3), 4);
	TestEqual(TEXT("blond hair"), FMRSpriteColours::HairXlat(13), 0x2F);
	// the layout carries each part's translation and class, and one untranslated atlas per bgf
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	const FMRSpriteLook* Male = Lib.Looks.Find(TEXT("test_male"));
	if (TestNotNull(TEXT("test_male"), Male))
	{
		const FMRSpritePart* Body = Male->Find(TEXT("body"));
		TestTrue(TEXT("body translation"), Body && Body->Xlat == 237 && Body->Atlas == TEXT("bta"));
		const FMRSpritePart* Weaponless = Male->Find(TEXT("head"));
		TestTrue(TEXT("head is skin"), Weaponless && Lib.MaterialClasses.IsValidIndex(Weaponless->Class)
			&& Lib.MaterialClasses[Weaponless->Class].Name == TEXT("skin"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRSpriteMonsterTest, "Meridian.Sprites.Monsters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRSpriteMonsterTest::RunTest(const FString& Parameters)
{
	// tools/sprites/monsters.py: every class has its look and corpse, with the Kod animations
	const FMRSpriteLibrary& Lib = FMRSpriteLibrary::Get();
	TestTrue(TEXT("monsters loaded"), Lib.Monsters.Num() > 0);
	for (const TPair<FName, FMRMonsterDef>& M : Lib.Monsters)
	{
		const FString C = M.Key.ToString();
		TestTrue(*(C + TEXT(" look")), Lib.Looks.Contains(M.Value.Look));
		TestTrue(*(C + TEXT(" corpse")), M.Value.DeadLook.IsNone() || Lib.Looks.Contains(M.Value.DeadLook));
		TestNotNull(*(C + TEXT(" stands")), Lib.FindAction(M.Value.Look, TEXT("stand")));
	}
	// giant rat (rat.kod SendMoveAnimation / SendAnimation): walk 2-6 every 75 ms, attack 7-11 once
	const FMRMonsterDef* Rat = Lib.Monsters.Find(TEXT("GiantRat"));
	if (TestNotNull(TEXT("GiantRat"), Rat))
	{
		const FMRSpriteAction* Walk = Lib.FindAction(Rat->Look, TEXT("walk"));
		const FMRSpriteTrackDef* W = Walk ? Walk->Tracks.Find(TEXT("body")) : nullptr;
		TestTrue(TEXT("rat walk"), W && W->Mode == FMRSpriteTrackDef::EMode::Cycle && W->Low == 2 && W->High == 6 && W->PeriodMs == 75);
		const FMRSpriteAction* Attack = Lib.FindAction(Rat->Look, TEXT("attack"));
		const FMRSpriteTrackDef* A = Attack ? Attack->Tracks.Find(TEXT("body")) : nullptr;
		TestTrue(TEXT("rat attack"), A && A->Mode == FMRSpriteTrackDef::EMode::Once && A->Low == 7 && A->High == 11);
		TestFalse(TEXT("rat is not aggressive"), Rat->bAggressive);
	}
	// a cow has no attack, and doesn't borrow the player's
	const FMRMonsterDef* Cow = Lib.Monsters.Find(TEXT("Cow"));
	if (TestNotNull(TEXT("Cow"), Cow))
	{
		TestNull(TEXT("cow attack"), Lib.FindAction(Cow->Look, TEXT("attack")));
		TestNull(TEXT("cow dance"), Lib.FindAction(Cow->Look, TEXT("dance")));
	}
	const FMRMonsterDef* Mummy = Lib.Monsters.Find(TEXT("Mummy"));
	TestTrue(TEXT("mummies are aggressive"), Mummy && Mummy->bAggressive);
	return true;
}

#endif

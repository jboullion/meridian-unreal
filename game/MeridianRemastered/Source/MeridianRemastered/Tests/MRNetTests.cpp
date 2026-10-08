// Automation tests for the Meridian protocol's pure parts (docs/adr/0010-meridian-servers.md).
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian.Net;Quit" -unattended -nullrhi
// The vectors come from an independent model of blakserv's C arithmetic; tools/ue/run_net_test.ps1
// checks the same code against a real server.

#include "Misc/AutomationTest.h"
#include "Net/MRAssetCache.h"
#include "Net/MRNetWorld.h"
#include "Net/MRProtocol.h"
#include "Net/MRResources.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRNetProtocolTest, "Meridian.Net.Protocol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRNetProtocolTest::RunTest(const FString& Parameters)
{
	// CRC-32 (blakserv util/crc.c is the standard reflected table)
	const char* Check = "123456789";
	TestEqual(TEXT("crc32 check value"), MRProto::Crc32(reinterpret_cast<const uint8*>(Check), 9), 0xCBF43926u);

	// fields
	FMRWriter W(MRMsg::BP_SAY_TO);
	W.U8(MRMsg::SAY_NORMAL).Str(TEXT("Hi é")).U16(0x1234).U32(0xDEADBEEF);
	FMRReader R(W.Bytes);
	TestEqual(TEXT("u8"), static_cast<int32>(R.U8()), static_cast<int32>(MRMsg::SAY_NORMAL));
	TestEqual(TEXT("latin-1 string"), R.Str(), FString(TEXT("Hi é")));
	TestEqual(TEXT("u16"), static_cast<int32>(R.U16()), 0x1234);
	TestEqual(TEXT("u32"), R.U32(), 0xDEADBEEFu);
	TestTrue(TEXT("read to the end"), R.IsOk() && R.AtEnd());
	R.U8();
	TestFalse(TEXT("past the end is an error"), R.IsOk());

	// frames split across WebSocket messages
	const TArray<uint8> Body = {5, 1, 2, 3};
	const TArray<uint8> Frame = MRProto::EncodeFrame(Body, 0x1234, 7);
	TestEqual(TEXT("frame size"), Frame.Num(), MRMsg::HeaderBytes + 4);
	FMRFrameDecoder Decoder;
	FMRFrame Out;
	Decoder.Append(Frame.GetData(), 3);
	TestFalse(TEXT("half a header"), Decoder.Next(Out));
	Decoder.Append(Frame.GetData() + 3, 5);
	TestFalse(TEXT("header and a byte"), Decoder.Next(Out));
	Decoder.Append(Frame.GetData() + 8, Frame.Num() - 8);
	Decoder.Append(Frame.GetData(), Frame.Num());  // a second frame right behind it
	TestTrue(TEXT("whole frame"), Decoder.Next(Out));
	TestEqual(TEXT("check word"), static_cast<int32>(Out.Check), 0x1234);
	TestEqual(TEXT("epoch"), static_cast<int32>(Out.Epoch), 7);
	TestTrue(TEXT("body"), Out.Body == Body);
	TestTrue(TEXT("second frame"), Decoder.Next(Out) && Out.Body == Body);
	TestFalse(TEXT("nothing left"), Decoder.Next(Out));
	const uint8 Bad[] = {4, 0, 0, 0, 5, 0, 0};
	Decoder.Append(Bad, 7);
	TestFalse(TEXT("mismatched lengths"), Decoder.Next(Out));
	TestTrue(TEXT("broken stream"), Decoder.IsBroken());

	// the client's security word: unsigned wrap-around, the type byte sign-extended (155 = BP_USERCOMMAND)
	FMRSecurityStreams Streams;
	const uint32 Seeds[5] = {1000, 2000, 3000, 4000, 4294967295u};
	Streams.Seed(Seeds);
	TestEqual(TEXT("word 1 (BP_REQ_MOVE)"), static_cast<int32>(Streams.Next({100, 0x40, 0x02, 0x80, 0x01, 25, 1, 0, 0, 0})), 12052);
	TestEqual(TEXT("word 2 (a type above 127)"), static_cast<int32>(Streams.Next({155, 5})), 44501);
	TestEqual(TEXT("word 3 (BP_PING)"), static_cast<int32>(Streams.Next({3})), 51153);

	// the server's type-byte token sliding along the redbook
	FMRServerToken Token;
	Token.Rekey(0x10, {'A', 'B', 'C'});
	const uint8 Scrambled[] = {50, 98, 215, 131};
	const uint8 Plain[] = {0x22, 0x33, 0x44, 0x55};
	for (int32 i = 0; i < 4; ++i)
	{
		TArray<uint8> B = {Scrambled[i], 9};
		Token.Decode(B);
		TestEqual(FString::Printf(TEXT("token decodes type %d"), i), static_cast<int32>(B[0]), static_cast<int32>(Plain[i]));
	}

	// AP_LOGIN's password: 16 MD5 bytes, no zeros
	const TArray<uint8> Digest = MRProto::PasswordDigest(TEXT("secret"));
	TestEqual(TEXT("digest length"), Digest.Num(), 16);
	TestFalse(TEXT("no zero bytes"), Digest.Contains(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRNetResourcesTest, "Meridian.Net.Resources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRNetResourcesTest::RunTest(const FString& Parameters)
{
	// a small rsb: magic, version 5, count, then (id, language, text NUL)
	TArray<uint8> Rsb = {'R', 'S', 'C', 1, 5, 0, 0, 0};
	auto Add = [&Rsb](uint32 Id, int32 Lang, const char* Text)
	{
		for (int32 i = 0; i < 4; ++i) Rsb.Add((Id >> (8 * i)) & 0xFF);
		for (int32 i = 0; i < 4; ++i) Rsb.Add((static_cast<uint32>(Lang) >> (8 * i)) & 0xFF);
		Rsb.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text) + 1);
	};
	Add(1, 0, "%s says, \"%q\"");
	Add(2, 0, "Bob");
	Add(3, 0, "You have %d shillings.");
	Add(4, 0, "%r and %s");
	Add(5, 0, "~BBold~n move, %s");
	Add(2, 1, "Robert");  // another language does not replace the default
	const int32 Count = 6;
	Rsb.Insert({static_cast<uint8>(Count), 0, 0, 0}, 8);
	FMRResourceTable Res;
	TestTrue(TEXT("rsb parses"), Res.Load(Rsb));
	TestEqual(TEXT("entries"), Res.Num(), 5);
	TestEqual(TEXT("language 0 wins"), Res.Get(2), FString(TEXT("Bob")));
	Res.SetDynamic(9, TEXT("Alice"));
	TestEqual(TEXT("dynamic resource"), Res.Get(9), FString(TEXT("Alice")));

	auto Format = [&Res](uint32 Fmt, const FMRWriter& Params)
	{
		FMRReader R(Params.Bytes);  // skips the writer's type byte
		FString Out;
		return MRServerText::Format(Res, Fmt, R, Out) ? Out : FString(TEXT("<error>"));
	};
	TestEqual(TEXT("%s and %q"), Format(1, FMRWriter(0).U32(9).Str(TEXT("hello"))), FString(TEXT("Alice says, \"hello\"")));
	TestEqual(TEXT("%d"), Format(3, FMRWriter(0).I32(42)), FString(TEXT("You have 42 shillings.")));
	TestEqual(TEXT("%r takes the following parameters"), Format(4, FMRWriter(0).U32(3).I32(7).U32(2)), FString(TEXT("You have 7 shillings. and Bob")));
	TestEqual(TEXT("missing parameter"), Format(3, FMRWriter(0)), FString(TEXT("<error>")));
	TestEqual(TEXT("style codes stripped"), MRServerText::StripStyle(Format(5, FMRWriter(0).U32(2))), FString(TEXT("Bold move, Bob")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRNetWorldTest, "Meridian.Net.World",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRNetWorldTest::RunTest(const FString& Parameters)
{
	// names for the records below: an rsb with ids 10.. (as Meridian.Net.Resources builds one)
	TArray<uint8> Rsb = {'R', 'S', 'C', 1, 5, 0, 0, 0, 6, 0, 0, 0};
	auto Add = [&Rsb](uint32 Id, const char* Text)
	{
		for (int32 i = 0; i < 4; ++i) Rsb.Add((Id >> (8 * i)) & 0xFF);
		for (int32 i = 0; i < 4; ++i) Rsb.Add(0);
		Rsb.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text) + 1);
	};
	Add(10, "shilling.bgf");
	Add(11, "shillings");
	Add(12, "raza.roo");
	Add(13, "Raza");
	Add(14, "phax.bgf");
	Add(15, "skya.bgf");
	FMRResourceTable Res;
	TestTrue(TEXT("rsb parses"), Res.Load(Rsb));

	// BP_PLAYER (clientd3d server.c HandlePlayer): ids, room, security, lights, background, wading sound, flags, 3 depths
	FMRWriter P(MRMsg::BP_PLAYER);
	P.U32(0x10000123).U32(14).U32(11).U32(77).U32(12).U32(13).U32(0xA1234567).U8(40).U8(3).U32(15).U32(0).U32(0x21).U32(1).U32(2).U32(3);
	FMRReader PR(P.Bytes);
	FMRNetPlayer Player;
	TestTrue(TEXT("BP_PLAYER reads"), MRNetRead::Player(PR, Res, Player));
	TestTrue(TEXT("BP_PLAYER read to its end"), PR.AtEnd());
	TestEqual(TEXT("player id without its tag"), Player.Id, 0x123u);
	TestEqual(TEXT("room file"), Player.RoomFile, FString(TEXT("raza.roo")));
	TestEqual(TEXT("ambient light"), static_cast<int32>(Player.AmbientLight), 40);
	TestEqual(TEXT("background"), Player.Background, FString(TEXT("skya.bgf")));
	TestEqual(TEXT("room flags"), Player.RoomFlags, 0x21u);
	TestEqual(TEXT("third depth"), Player.Depth[2], 3u);

	// a number item (shillings) with a light, an effect prefix, a cycling animation and one overlay
	FMRWriter O(MRMsg::BP_CREATE);
	O.U32(0x10000042).U32(250).U32(10).U32(11).U32(MRMsg::OF_GETTABLE).U8(2).U32(0x8).U32(0x00FF8000).U8(1).U8(0);
	O.U16(1).U8(200).U16(0x7FFF);                                             // light: flags, intensity, colour
	O.U8(MRMsg::ANIMATE_EFFECT).U8(5);                                        // drawing effect prefix
	O.U8(MRMsg::ANIMATE_CYCLE).U32(150).U16(1).U16(4);                        // animation
	O.U8(1).U32(14).U8(13).U8(MRMsg::ANIMATE_TRANSLATION).U8(9).U8(MRMsg::ANIMATE_NONE).U16(2);  // one overlay
	O.U16(64 * 5 + 10).U16(64 * 7).U16(1024);                                 // position, angle
	O.U8(MRMsg::ANIMATE_NONE).U16(3).U8(0);                                   // motion: animation, no overlays
	FMRReader OR(O.Bytes);
	FMRNetObject Obj;
	TestTrue(TEXT("room object reads"), MRNetRead::RoomObject(OR, Res, Obj));
	TestTrue(TEXT("room object read to its end"), OR.AtEnd());
	TestEqual(TEXT("object id"), Obj.Id, 0x42u);
	TestEqual(TEXT("amount of a number item"), Obj.Amount, 250u);
	TestEqual(TEXT("drawing effect"), static_cast<int32>(Obj.DrawEffect), 2);
	TestEqual(TEXT("name colour"), Obj.NameColor, 0x00FF8000u);
	TestEqual(TEXT("light intensity"), static_cast<int32>(Obj.Light.Intensity), 200);
	TestEqual(TEXT("effect prefix"), Obj.Effect, 5);
	TestEqual(TEXT("no translation"), Obj.Xlat, -1);
	TestEqual(TEXT("animation cycles"), static_cast<int32>(Obj.Animation.Type), static_cast<int32>(MRMsg::ANIMATE_CYCLE));
	TestEqual(TEXT("animation's last group"), static_cast<int32>(Obj.Animation.GroupHigh), 4);
	TestTrue(TEXT("overlay"), Obj.OverlayParts.Num() == 1 && Obj.OverlayParts[0].Bgf == TEXT("phax") && Obj.OverlayParts[0].Hotspot == 13
		&& Obj.OverlayParts[0].Xlat == 9);
	TestEqual(TEXT("row"), Obj.KodRow, 64 * 5 + 10);
	TestEqual(TEXT("motion animation"), static_cast<int32>(Obj.MotionAnimation.Group), 3);
	TestEqual(TEXT("name"), Obj.Name, FString(TEXT("shillings")));

	// BP_CHANGE: an object without a light (no intensity or colour follow), then its new motion record
	FMRWriter Dark(MRMsg::BP_CHANGE);
	Dark.U32(7).U32(10).U32(11).U32(0).U8(0).U32(0).U32(0).U8(0).U8(0).U16(MRMsg::LIGHT_FLAG_NONE).U8(MRMsg::ANIMATE_NONE).U16(1).U8(0);
	Dark.U8(MRMsg::ANIMATE_TRANSLATION).U8(4).U8(MRMsg::ANIMATE_NONE).U16(2).U8(0);
	FMRReader DR(Dark.Bytes);
	FMRNetObject DarkObj;
	TestTrue(TEXT("BP_CHANGE reads to its end"), MRNetRead::Object(DR, Res, DarkObj) && MRNetRead::Motion(DR, Res, DarkObj) && DR.AtEnd());
	TestEqual(TEXT("BP_CHANGE's motion translation"), DarkObj.MotionXlat, 4);

	// a players-list entry (its name is a string, not a resource)
	FMRWriter U(MRMsg::BP_PLAYER_ADD);
	U.U32(99).U32(5000).Str(TEXT("Frenzy")).U32(MRMsg::OF_PLAYER).U8(0).U32(0).U32(0xFFFFFF).U8(0).U8(0);
	FMRReader UR(U.Bytes);
	FMRNetUser User;
	TestTrue(TEXT("user reads to its end"), MRNetRead::User(UR, User) && UR.AtEnd());
	TestEqual(TEXT("user name"), User.Name, FString(TEXT("Frenzy")));

	// the room's security value and the 28 bits compared
	TArray<uint8> Roo = {'R', 'O', 'O', 0xB1, 15, 0, 0, 0, 0x67, 0x45, 0x23, 0xA1};
	TestEqual(TEXT("roo security"), MRNetRead::RooSecurity(Roo), 0xA1234567u);
	TestTrue(TEXT("the top 4 bits don't count"), MRNetRead::SecurityMatches(0xA1234567u, 0x01234567u));
	TestFalse(TEXT("a different room"), MRNetRead::SecurityMatches(0xA1234567u, 0xA1234568u));

	// BP_LOOK: an object, flags, its description (a format and its parameters), then an inscription
	Add(16, "A %s for sale.");
	Add(17, "Welcome to Raza.");
	Rsb[8] = 8;  // the entry count
	FMRResourceTable Res2;
	TestTrue(TEXT("rsb with descriptions parses"), Res2.Load(Rsb));
	FMRWriter L(MRMsg::BP_LOOK);
	L.U32(7).U32(10).U32(11).U32(MRMsg::OF_SIGN).U8(0).U32(0).U32(0xFFFFFF).U8(0).U8(0).U16(0).U8(MRMsg::ANIMATE_NONE).U16(1).U8(0);
	L.U8(MRMsg::DF_INSCRIBED).U32(16).U32(11).U32(17);
	FMRReader LR(L.Bytes);
	FMRNetDescription D;
	TestTrue(TEXT("BP_LOOK reads to its end"), MRNetRead::Look(LR, Res2, D) && LR.AtEnd());
	TestEqual(TEXT("BP_LOOK description"), D.Text, FString(TEXT("A shillings for sale.")));
	TestEqual(TEXT("BP_LOOK inscription"), D.Inscription, FString(TEXT("Welcome to Raza.")));

	// UC_LOOK_PLAYER (after BP_USERCOMMAND's two type bytes): object, editable, description, extra, web page
	FMRWriter P2(MRMsg::BP_USERCOMMAND);
	P2.U8(MRMsg::UC_LOOK_PLAYER);
	P2.U32(0x123).U32(14).U32(11).U32(MRMsg::OF_PLAYER).U8(0).U32(MRMsg::MM_PLAYER).U32(0xFFFFFF).U8(0).U8(0).U16(0).U8(MRMsg::ANIMATE_NONE).U16(1).U8(0);
	P2.U8(1).U32(17).U32(16).U32(11).Str(TEXT("http://example.org"));
	FMRReader PR2(P2.Bytes, 2);
	FMRNetDescription PD;
	TestTrue(TEXT("UC_LOOK_PLAYER reads to its end"), MRNetRead::LookPlayer(PR2, Res2, PD) && PR2.AtEnd());
	TestTrue(TEXT("one's own description is editable"), PD.bPlayer && PD.bEditable);
	TestEqual(TEXT("player web page"), PD.Url, FString(TEXT("http://example.org")));

	// BP_PLAYER_OVERLAY's object has no light (ExtractObjectNoLight)
	FMRWriter OV(MRMsg::BP_PLAYER_OVERLAY);
	OV.U8(4).U32(2).U32(10).U32(0).U32(0).U8(0).U32(0).U32(0).U8(0).U8(0).U8(MRMsg::ANIMATE_ONCE).U32(175).U16(1).U16(3).U16(0).U8(0);
	FMRReader OVR(OV.Bytes, 2);
	FMRNetObject Hand;
	TestTrue(TEXT("player overlay reads to its end"), MRNetRead::ObjectNoLight(OVR, Res2, Hand) && OVR.AtEnd());
	TestEqual(TEXT("player overlay animation"), static_cast<int32>(Hand.Animation.Type), static_cast<int32>(MRMsg::ANIMATE_ONCE));

	// BP_INVENTORY: a list of objects; a number item's id carries CLIENT_TAG_NUMBER and an amount
	FMRWriter INV(MRMsg::BP_INVENTORY);
	INV.U16(2);
	INV.U32(0x200).U32(10).U32(0).U32(0).U8(0).U32(0).U32(0).U8(0).U8(0).U16(0).U8(MRMsg::ANIMATE_NONE).U16(1).U8(0);
	INV.U32(MRMsg::NumberId(0x201)).U32(57).U32(10).U32(0).U32(0).U8(0).U32(0).U32(0).U8(0).U8(0).U16(0).U8(MRMsg::ANIMATE_NONE).U16(1).U8(0);
	FMRReader INVR(INV.Bytes, 1);
	TArray<FMRNetObject> Carried;
	TestTrue(TEXT("inventory reads to its end"), MRNetRead::ObjectList(INVR, Res2, Carried) && INVR.AtEnd());
	if (TestEqual(TEXT("two items"), Carried.Num(), 2))
	{
		TestTrue(TEXT("a plain item"), Carried[0].Id == 0x200 && !Carried[0].bNumber && Carried[0].Amount == 0);
		TestTrue(TEXT("a number item: its plain id and amount"), Carried[1].Id == 0x201 && Carried[1].bNumber && Carried[1].Amount == 57);
	}
	TestEqual(TEXT("a number item is sent tagged"), MRMsg::NumberId(0x201), 0x10000201u);
	FMRWriter USE(MRMsg::BP_USE_LIST);
	USE.U16(2).U32(0x200).U32(0x202);
	FMRReader USER(USE.Bytes, 1);
	TArray<uint32> InUse;
	TestTrue(TEXT("use list reads"), MRNetRead::IdList(USER, InUse) && USER.AtEnd() && InUse.Num() == 2 && InUse[1] == 0x202);

	// the manifest's hash: the first 16 hex digits of SHA-1 ("abc": a9993e36 4706816a ...)
	TestEqual(TEXT("manifest hash"), FMRAssetCache::HashBytes({'a', 'b', 'c'}), FString(TEXT("a9993e364706816a")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRNetCombatTest, "Meridian.Net.Combat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRNetCombatTest::RunTest(const FString& Parameters)
{
	// battler.kod's hit messages as Kod compiles them, and their parameters' resources
	TArray<uint8> Rsb = {'R', 'S', 'C', 1, 5, 0, 0, 0, 0, 0, 0, 0};
	int32 Count = 0;
	auto Add = [&Rsb, &Count](uint32 Id, const char* Text)
	{
		for (int32 i = 0; i < 4; ++i) Rsb.Add((Id >> (8 * i)) & 0xFF);
		for (int32 i = 0; i < 4; ++i) Rsb.Add(0);
		Rsb.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text) + 1);
		Rsb[8] = static_cast<uint8>(++Count);
	};
	Add(20, "%sYour %s %s %s%q for ~k~B%i~B%s damage.");
	Add(21, "%sYour %s %s %s%s for ~k~B%i~B%s damage.");
	Add(22, "%s%s%q's %s %s you for ~r~B%i~B%s damage.");
	Add(23, "%s%s%s's %s %s you for ~r~B%i~B%s damage.");
	Add(30, "~b");
	Add(31, "mace");
	Add(32, "wounds");
	Add(33, "the ");
	Add(34, "rat");
	Add(35, "The ");
	Add(36, "bite");
	Add(40, "arrow.bgf");
	FMRResourceTable Res;
	TestTrue(TEXT("rsb parses"), Res.Load(Rsb));
	TestTrue(TEXT("a format found by its text"), Res.FindByText(TEXT("%sYour %s %s %s%s for ~k~B%i~B%s damage.")) == TArray<uint32>({21}));

	// "Your mace wounds the rat for 4 damage."
	FMRWriter M(MRMsg::BP_MESSAGE);
	M.U32(21).U32(30).U32(31).U32(32).U32(33).U32(34).I32(4).U32(30);
	FMRReader MR(M.Bytes, 5);
	FMRNetHit Hit;
	TestTrue(TEXT("we hit a monster"), MRNetRead::Hit(MR, Res, 2, Hit) && Hit.bDealt && Hit.Name == TEXT("rat") && Hit.Damage == 4);
	FString Text;
	FMRReader MR2(M.Bytes, 5);
	TestTrue(TEXT("and the line still reads"), MRServerText::Format(Res, 21, MR2, Text) && MRServerText::StripStyle(Text) == TEXT("Your mace wounds the rat for 4 damage."));
	// "Your mace wounds Frenzy for 12 damage." (a player's name is a string)
	FMRWriter P(MRMsg::BP_MESSAGE);
	P.U32(20).U32(30).U32(31).U32(32).U32(0).Str(TEXT("Frenzy")).I32(12).U32(30);
	TestTrue(TEXT("we hit a player"), MRNetRead::Hit(FMRReader(P.Bytes, 5), Res, 1, Hit) && Hit.bDealt && Hit.Name == TEXT("Frenzy") && Hit.Damage == 12);
	// "The rat's bite wounds you for 2 damage."
	FMRWriter D(MRMsg::BP_MESSAGE);
	D.U32(23).U32(30).U32(35).U32(34).U32(36).U32(32).I32(2).U32(30);
	TestTrue(TEXT("a monster hit us"), MRNetRead::Hit(FMRReader(D.Bytes, 5), Res, 4, Hit) && !Hit.bDealt && Hit.Name == TEXT("rat") && Hit.Damage == 2);

	// BP_EFFECT (clientd3d effect.c): durations, a flash's translation, the blur adding up, the weather
	FMRNetEffects E;
	auto Effect = [&E](FMRWriter W) { FMRReader R(W.Bytes, 1); return E.Apply(R) && R.AtEnd(); };
	TestTrue(TEXT("pain"), Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(MRMsg::EFFECT_PAIN).I32(1500))) && E.PainMs == 1500.f);
	TestTrue(TEXT("pain is at most 10 s"), Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(MRMsg::EFFECT_PAIN).I32(99999))) && E.PainMs == 10000.f);
	TestTrue(TEXT("a red flash"), Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(MRMsg::EFFECT_FLASHXLAT).I32(300).I32(0x45))) && E.FlashXlat == 0x45 && E.FlashMs == 300.f);
	Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(MRMsg::EFFECT_BLUR).I32(150000)));
	TestTrue(TEXT("blur adds up to 200 s"), Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(MRMsg::EFFECT_BLUR).I32(150000))) && E.BlurMs == 200000.f);
	TestTrue(TEXT("paralysis has no parameters"), Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(MRMsg::EFFECT_PARALYZE))) && E.bParalyzed);
	TestTrue(TEXT("rain"), Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(MRMsg::EFFECT_RAINING))) && E.Weather == MRMsg::EFFECT_RAINING);
	TestTrue(TEXT("cleared"), Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(MRMsg::EFFECT_CLEARWEATHER))) && E.Weather == 0);
	E.Tick(400.f);
	TestTrue(TEXT("the flash is over, the pain fades"), E.FlashXlat == 0 && E.PainMs == 9600.f);
	TestFalse(TEXT("an unknown effect"), Effect(MoveTemp(FMRWriter(MRMsg::BP_EFFECT).U16(99))));

	// BP_SHOOT (server.c HandleShoot): icon, translation, animation, source, dest, speed, flags, light
	FMRWriter S(MRMsg::BP_SHOOT);
	S.U32(40).U8(MRMsg::ANIMATE_TRANSLATION).U8(7).U8(MRMsg::ANIMATE_CYCLE).U32(100).U16(1).U16(2);
	S.U32(0x101).U32(0x102).U8(12).U16(MRMsg::PROJ_FLAG_FOLLOWGROUND).U16(3).U8(80).U16(0x7C00);
	FMRReader SR(S.Bytes, 1);
	FMRNetProjectile Shot;
	TestTrue(TEXT("BP_SHOOT reads to its end"), MRNetRead::Projectile(SR, Res, false, Shot) && SR.AtEnd());
	TestTrue(TEXT("an arrow from one to the other"), Shot.Icon == TEXT("arrow.bgf") && Shot.Xlat == 7 && Shot.Source == 0x101 && Shot.Dest == 0x102
		&& Shot.Speed == 12 && Shot.Flags == MRMsg::PROJ_FLAG_FOLLOWGROUND && Shot.Light.Intensity == 80 && Shot.Light.Color == 0x7C00);
	// BP_RADIUS_SHOOT: no dest; range and number after the flags; no light
	FMRWriter RS(MRMsg::BP_RADIUS_SHOOT);
	RS.U32(40).U8(MRMsg::ANIMATE_NONE).U16(1).U32(0x101).U8(8).U16(0).U8(5).U8(8).U16(0);
	FMRReader RSR(RS.Bytes, 1);
	TestTrue(TEXT("BP_RADIUS_SHOOT reads to its end"), MRNetRead::Projectile(RSR, Res, true, Shot) && RSR.AtEnd());
	TestTrue(TEXT("eight out to 5 squares"), Shot.bRadius && Shot.Range == 5 && Shot.Number == 8 && Shot.Dest == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRNetSpellsTest, "Meridian.Net.Spells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRNetSpellsTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Rsb = {'R', 'S', 'C', 1, 5, 0, 0, 0, 0, 0, 0, 0};
	int32 Count = 0;
	auto Add = [&Rsb, &Count](uint32 Id, const char* Text)
	{
		for (int32 i = 0; i < 4; ++i) Rsb.Add((Id >> (8 * i)) & 0xFF);
		for (int32 i = 0; i < 4; ++i) Rsb.Add(0);
		Rsb.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text) + 1);
		Rsb[8] = static_cast<uint8>(++Count);
	};
	Add(10, "imeditate.bgf");
	Add(11, "meditate");
	Add(12, "iappraise.bgf");
	Add(13, "appraise");
	Add(14, "rmnocombat.bgf");
	Add(15, "Safe Room");
	FMRResourceTable Res;
	TestTrue(TEXT("rsb parses"), Res.Load(Rsb));
	// an object as ExtractObject reads it: id, icon, name, flags, drawing effect, minimap, name colour, types, no light, animation, no overlays
	auto Object = [](FMRWriter& W, uint32 Id, uint32 Icon, uint32 Name)
	{
		W.U32(Id).U32(Icon).U32(Name).U32(0).U8(0).U32(0).U32(0).U8(0).U8(0).U16(0).U8(MRMsg::ANIMATE_NONE).U16(1).U8(0);
	};

	// BP_SPELLS (merintr.c HandleSpells): u16 count, then each spell: the object, u8 targets, u8 school
	FMRWriter SP(MRMsg::BP_SPELLS);
	SP.U16(2);
	Object(SP, 0x300, 10, 11);
	SP.U8(0).U8(7);
	Object(SP, 0x301, 12, 13);
	SP.U8(1).U8(7);
	FMRReader SPR(SP.Bytes, 1);
	TArray<FMRNetSpell> Spells;
	TestTrue(TEXT("BP_SPELLS reads to its end"), MRNetRead::SpellList(SPR, Res, Spells) && SPR.AtEnd());
	if (TestEqual(TEXT("two spells"), Spells.Num(), 2))
	{
		TestTrue(TEXT("meditate takes no target"), Spells[0].Object.Name == TEXT("meditate") && Spells[0].Targets == 0 && Spells[0].School == 7);
		TestTrue(TEXT("appraise takes one"), Spells[1].Object.Id == 0x301 && Spells[1].Targets == 1);
	}

	// BP_ADD_ENCHANTMENT (merintr.c HandleAddEnchantment): u8 type, then the enchantment as an object
	FMRWriter EN(MRMsg::BP_ADD_ENCHANTMENT);
	EN.U8(MRMsg::ENCHANT_ROOM);
	Object(EN, 0x400, 14, 15);
	FMRReader ENR(EN.Bytes, 1);
	FMRNetObject Ench;
	TestTrue(TEXT("a room enchantment"), ENR.U8() == MRMsg::ENCHANT_ROOM && MRNetRead::Object(ENR, Res, Ench) && ENR.AtEnd()
		&& Ench.Name == TEXT("Safe Room") && Ench.Icon == TEXT("rmnocombat.bgf"));

	// BP_STAT_CHANGE (user.kod SendStatChange): six stats, eight school levels
	FMRWriter SC(MRMsg::BP_REQ_STAT_CHANGE);
	const uint8 OfferBytes[] = {50, 10, 30, 35, 40, 35, 1, 0, 0, 2, 0, 0, 1, 0};
	for (const uint8 V : OfferBytes)
	{
		SC.U8(V);
	}
	FMRReader SCR(SC.Bytes, 1);
	FMRNetStatChange Offer;
	TestTrue(TEXT("BP_STAT_CHANGE reads to its end"), MRNetRead::StatChange(SCR, Offer) && SCR.AtEnd());
	TestTrue(TEXT("its stats and levels"), Offer.Stats[0] == 50 && Offer.Stats[5] == 35 && Offer.Levels[3] == 2 && Offer.Levels[6] == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMRNetTradeTest, "Meridian.Net.Trade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMRNetTradeTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Rsb = {'R', 'S', 'C', 1, 5, 0, 0, 0, 0, 0, 0, 0};
	int32 Count = 0;
	auto Add = [&Rsb, &Count](uint32 Id, const char* Text)
	{
		for (int32 i = 0; i < 4; ++i) Rsb.Add((Id >> (8 * i)) & 0xFF);
		for (int32 i = 0; i < 4; ++i) Rsb.Add(0);
		Rsb.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text) + 1);
		Rsb[8] = static_cast<uint8>(++Count);
	};
	Add(10, "smith.bgf");
	Add(11, "Tomas");
	Add(12, "torch.bgf");
	Add(13, "torch");
	Add(14, "coin.bgf");
	Add(15, "shilling");
	FMRResourceTable Res;
	TestTrue(TEXT("rsb parses"), Res.Load(Rsb));
	auto Object = [](FMRWriter& W, uint32 Id, uint32 Icon, uint32 Name)
	{
		W.U32(Id).U32(Icon).U32(Name).U32(0).U8(0).U32(0).U32(0).U8(0).U8(0).U16(0).U8(MRMsg::ANIMATE_NONE).U16(1).U8(0);
	};
	// BP_BUY_LIST (server.c HandleBuyList): the seller, u16 count, each item and its u32 price
	FMRWriter B(MRMsg::BP_BUY_LIST);
	Object(B, 0x500, 10, 11);
	B.U16(2);
	Object(B, 0x501, 12, 13);
	B.U32(36);
	B.U32(MRMsg::NumberId(0x502)).U32(500).U32(14).U32(15).U32(0).U8(0).U32(0).U32(0).U8(0).U8(0).U16(0).U8(MRMsg::ANIMATE_NONE).U16(1).U8(0);
	B.U32(1);
	FMRReader BR(B.Bytes, 1);
	FMRNetShop Shop;
	TestTrue(TEXT("BP_BUY_LIST reads to its end"), MRNetRead::BuyList(BR, Res, Shop) && BR.AtEnd());
	TestTrue(TEXT("Tomas sells a torch for 36"), Shop.Seller.Name == TEXT("Tomas") && Shop.Items.Num() == 2 && Shop.Items[0].Object.Name == TEXT("torch")
		&& Shop.Items[0].Price == 36);
	TestTrue(TEXT("and shillings as a number item"), Shop.Items.Num() == 2 && Shop.Items[1].Object.bNumber && Shop.Items[1].Object.Id == 0x502 && Shop.Items[1].Price == 1);
	// BP_OFFER (server.c HandleOffer): who offers, then a list of objects
	FMRWriter O(MRMsg::BP_OFFER);
	Object(O, 0x600, 10, 11);
	O.U16(1);
	Object(O, 0x601, 12, 13);
	FMRReader OR(O.Bytes, 1);
	FMRNetObject Who;
	TArray<FMRNetObject> Items;
	TestTrue(TEXT("BP_OFFER reads to its end"), MRNetRead::Object(OR, Res, Who) && MRNetRead::ObjectList(OR, Res, Items) && OR.AtEnd()
		&& Who.Id == 0x600 && Items.Num() == 1 && Items[0].Name == TEXT("torch"));
	return true;
}

#endif

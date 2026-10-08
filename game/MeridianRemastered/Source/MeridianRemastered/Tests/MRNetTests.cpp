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

	// the manifest's hash: the first 16 hex digits of SHA-1 ("abc": a9993e36 4706816a ...)
	TestEqual(TEXT("manifest hash"), FMRAssetCache::HashBytes({'a', 'b', 'c'}), FString(TEXT("a9993e364706816a")));
	return true;
}

#endif

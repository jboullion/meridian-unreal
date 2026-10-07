// Automation tests for the Meridian protocol's pure parts (docs/adr/0010-meridian-servers.md).
// Run: UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Meridian.Net;Quit" -unattended -nullrhi
// The vectors come from an independent model of blakserv's C arithmetic; tools/ue/run_net_test.ps1
// checks the same code against a real server.

#include "Misc/AutomationTest.h"
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

#endif

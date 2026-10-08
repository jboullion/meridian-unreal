#include "Net/MRProtocol.h"

#include "Misc/SecureHash.h"

// ------------------------------------------------------------------------------ FMRWriter

FMRWriter& FMRWriter::U16(uint16 V)
{
	Bytes.Add(V & 0xFF);
	Bytes.Add(V >> 8);
	return *this;
}

FMRWriter& FMRWriter::U32(uint32 V)
{
	for (int32 i = 0; i < 4; ++i)
	{
		Bytes.Add((V >> (8 * i)) & 0xFF);
	}
	return *this;
}

FMRWriter& FMRWriter::Str(const FString& S)
{
	return Raw(MRProto::Latin1(S));
}

FMRWriter& FMRWriter::Raw(const TArray<uint8>& Data)
{
	U16(static_cast<uint16>(Data.Num()));
	Bytes.Append(Data);
	return *this;
}

// ------------------------------------------------------------------------------ FMRReader

bool FMRReader::Need(int32 Bytes)
{
	if (bError || Pos + Bytes > Num)
	{
		bError = true;
		return false;
	}
	return true;
}

uint8 FMRReader::U8()
{
	return Need(1) ? Data[Pos++] : 0;
}

uint16 FMRReader::U16()
{
	if (!Need(2))
	{
		return 0;
	}
	const uint16 V = Data[Pos] | (Data[Pos + 1] << 8);
	Pos += 2;
	return V;
}

uint32 FMRReader::U32()
{
	if (!Need(4))
	{
		return 0;
	}
	const uint32 V = Data[Pos] | (Data[Pos + 1] << 8) | (Data[Pos + 2] << 16) | (static_cast<uint32>(Data[Pos + 3]) << 24);
	Pos += 4;
	return V;
}

FString FMRReader::Str()
{
	const int32 Len = U16();
	if (!Need(Len))
	{
		return FString();
	}
	// Latin-1: one byte is one character (never UTF-8)
	FString S;
	S.Reserve(Len);
	for (int32 i = 0; i < Len; ++i)
	{
		S.AppendChar(static_cast<TCHAR>(Data[Pos + i]));
	}
	Pos += Len;
	return S;
}

void FMRReader::Skip(int32 Bytes)
{
	if (Need(Bytes))
	{
		Pos += Bytes;
	}
}

// ------------------------------------------------------------------------------ FMRFrameDecoder

bool FMRFrameDecoder::Next(FMRFrame& Out)
{
	if (bBroken || Buffer.Num() < MRMsg::HeaderBytes)
	{
		return false;
	}
	const uint16 Len = Buffer[0] | (Buffer[1] << 8);
	const uint16 Check = Buffer[2] | (Buffer[3] << 8);
	const uint16 LenAgain = Buffer[4] | (Buffer[5] << 8);
	if (Len != LenAgain || Len > MRMsg::MaxBody)
	{
		bBroken = true;
		return false;
	}
	if (Buffer.Num() < MRMsg::HeaderBytes + Len)
	{
		return false;
	}
	Out.Check = Check;
	Out.Epoch = Buffer[6];
	Out.Body.Reset(Len);
	Out.Body.Append(Buffer.GetData() + MRMsg::HeaderBytes, Len);
	Buffer.RemoveAt(0, MRMsg::HeaderBytes + Len, EAllowShrinking::No);
	return true;
}

// ------------------------------------------------------------------------------ MRProto

namespace
{
	struct FCrcTable
	{
		uint32 T[256];
		FCrcTable()
		{
			for (uint32 i = 0; i < 256; ++i)
			{
				uint32 C = i;
				for (int32 k = 0; k < 8; ++k)
				{
					C = (C & 1) ? (0xEDB88320u ^ (C >> 1)) : (C >> 1);
				}
				T[i] = C;
			}
		}
	};
}

uint32 MRProto::Crc32(const uint8* Data, int32 Num)
{
	static const FCrcTable Table;
	uint32 Crc = 0xFFFFFFFFu;
	for (int32 i = 0; i < Num; ++i)
	{
		Crc = (Crc >> 8) ^ Table.T[(Crc ^ Data[i]) & 0xFF];
	}
	return Crc ^ 0xFFFFFFFFu;
}

TArray<uint8> MRProto::EncodeFrame(const TArray<uint8>& Body, uint16 Check, uint8 Epoch)
{
	const uint16 Len = static_cast<uint16>(Body.Num());
	TArray<uint8> Out;
	Out.Reserve(MRMsg::HeaderBytes + Len);
	Out.Add(Len & 0xFF);
	Out.Add(Len >> 8);
	Out.Add(Check & 0xFF);
	Out.Add(Check >> 8);
	Out.Add(Len & 0xFF);
	Out.Add(Len >> 8);
	Out.Add(Epoch);
	Out.Append(Body);
	return Out;
}

TArray<uint8> MRProto::Latin1(const FString& S)
{
	TArray<uint8> Out;
	Out.Reserve(S.Len());
	for (const TCHAR C : S)
	{
		Out.Add(C <= 0xFF ? static_cast<uint8>(C) : static_cast<uint8>('?'));
	}
	return Out;
}

TArray<uint8> MRProto::PasswordDigest(const FString& Password)
{
	const TArray<uint8> Bytes = Latin1(Password);
	uint8 Digest[16];
	FMD5 Md5;
	Md5.Update(Bytes.GetData(), Bytes.Num());
	Md5.Final(Digest);
	TArray<uint8> Out;
	for (const uint8 B : Digest)
	{
		Out.Add(B == 0 ? 1 : B);  // the server keeps it as a C string
	}
	return Out;
}

// ------------------------------------------------------------------------------ security

uint16 FMRSecurityStreams::Next(const TArray<uint8>& Body)
{
	// unsigned 32-bit arithmetic, wrapping like the server's
	for (uint32& S : Seeds)
	{
		S = (S * 9301u + 49297u) % 233280u;
	}
	const uint32 R = Seeds[Seeds[4] % 4];
	uint32 Word = R & 0xFFFF;
	Word ^= static_cast<uint32>(Body.Num());
	// the type byte is a signed char on the server: 0x80 and up sign-extend before the shift
	const int32 TypeSigned = Body.Num() > 0 ? static_cast<int32>(static_cast<int8>(Body[0])) : 0;
	Word ^= static_cast<uint32>(TypeSigned) << 4;
	Word ^= MRProto::Crc16(Body);
	return static_cast<uint16>(Word & 0xFFFF);
}

void FMRServerToken::Decode(TArray<uint8>& Body)
{
	if (Body.Num() == 0)
	{
		return;
	}
	Body[0] ^= static_cast<uint8>(Token & 0xFF);
	if (Redbook.Num() > 0)
	{
		Token += Redbook[Pos] & 0x7F;
		if (++Pos >= Redbook.Num())
		{
			Pos = 0;
		}
	}
}

void FMRServerToken::Rekey(uint8 TokenByte, const TArray<uint8>& InRedbook)
{
	Token = TokenByte;
	Redbook = InRedbook;
	Pos = 0;
}


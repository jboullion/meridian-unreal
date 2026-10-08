#include "World/MRRooFile.h"

#include "Net/MRProtocol.h"

namespace
{
	float F32(FMRReader& R)
	{
		const uint32 Bits = R.U32();
		float V;
		FMemory::Memcpy(&V, &Bits, sizeof(V));
		return V;
	}

	int16 I16(FMRReader& R)
	{
		return static_cast<int16>(R.U16());
	}

	void ReadSlope(FMRReader& R, FMRRooSlope& Out)
	{
		Out.A = F32(R);
		Out.B = F32(R);
		Out.C = F32(R);
		Out.D = F32(R);
		Out.TexX = R.I32();
		Out.TexY = R.I32();
		Out.TexAngle = R.I32();
		R.Skip(18);
	}
}

bool FMRRooFile::Load(const TArray<uint8>& Bytes, FString& OutError)
{
	const uint8* Data = Bytes.GetData();
	const int32 Num = Bytes.Num();
	const uint8 Magic[4] = {0x52, 0x4F, 0x4F, 0xB1};
	if (Num < 16 || FMemory::Memcmp(Data, Magic, 4) != 0)
	{
		OutError = TEXT("not a room file");
		return false;
	}
	FMRReader Head(Data, Num, 4);
	Version = Head.I32();
	Security = Head.U32();
	const int32 MainInfo = Head.I32();
	if (Version < 13)
	{
		OutError = FString::Printf(TEXT("room version %d is too old"), Version);
		return false;
	}

	FMRReader Info(Data, Num, MainInfo);
	Width = Info.I32();
	Height = Info.I32();
	const int32 NodePos = Info.I32();
	const int32 WallPos = Info.I32();
	Info.I32();  // the server's walls
	const int32 SidedefPos = Info.I32();
	const int32 SectorPos = Info.I32();
	if (!Info.IsOk())
	{
		OutError = TEXT("cut short in its main info");
		return false;
	}

	FMRReader N(Data, Num, NodePos);
	Nodes.SetNum(N.U16());
	for (FMRRooNode& Node : Nodes)
	{
		Node.Type = N.U8();
		const float X0 = F32(N), Y0 = F32(N), X1 = F32(N), Y1 = F32(N);
		Node.Bounds = FBox2f(FVector2f(FMath::Min(X0, X1), FMath::Min(Y0, Y1)), FVector2f(FMath::Max(X0, X1), FMath::Max(Y0, Y1)));
		if (Node.Type == FMRRooNode::Internal)
		{
			Node.A = F32(N);
			Node.B = F32(N);
			Node.C = F32(N);
			Node.Pos = N.U16();
			Node.Neg = N.U16();
			Node.FirstWall = N.U16();
		}
		else if (Node.Type == FMRRooNode::Leaf)
		{
			Node.Sector = N.U16();
			Node.Points.SetNum(N.U16());
			for (FVector2f& P : Node.Points)
			{
				P.X = F32(N);
				P.Y = F32(N);
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("unknown node type %d"), Node.Type);
			return false;
		}
		if (!N.IsOk())
		{
			break;
		}
	}

	FMRReader W(Data, Num, WallPos);
	Walls.SetNum(W.U16());
	for (FMRRooWall& Wall : Walls)
	{
		Wall.Next = W.U16();
		Wall.PosSidedef = W.U16();
		Wall.NegSidedef = W.U16();
		Wall.X0 = F32(W);
		Wall.Y0 = F32(W);
		Wall.X1 = F32(W);
		Wall.Y1 = F32(W);
		Wall.Length = F32(W);
		Wall.PosXOffset = I16(W);
		Wall.NegXOffset = I16(W);
		Wall.PosYOffset = I16(W);
		Wall.NegYOffset = I16(W);
		Wall.PosSector = W.U16();
		Wall.NegSector = W.U16();
	}

	FMRReader S(Data, Num, SidedefPos);
	Sidedefs.SetNum(S.U16());
	for (FMRRooSidedef& Side : Sidedefs)
	{
		Side.ServerId = S.U16();
		Side.NormalTexture = S.U16();
		Side.AboveTexture = S.U16();
		Side.BelowTexture = S.U16();
		Side.Flags = S.U32();
		Side.AnimationSpeed = S.U8();
	}

	FMRReader C(Data, Num, SectorPos);
	Sectors.SetNum(C.U16());
	for (FMRRooSector& Sec : Sectors)
	{
		Sec.ServerId = C.U16();
		Sec.FloorTexture = C.U16();
		Sec.CeilingTexture = C.U16();
		Sec.TexX = I16(C);
		Sec.TexY = I16(C);
		Sec.FloorHeight = I16(C);
		Sec.CeilingHeight = I16(C);
		Sec.Light = C.U8();
		Sec.Flags = C.U32();
		Sec.AnimationSpeed = Version >= 10 ? C.U8() : 0;
		Sec.bSlopedFloor = (Sec.Flags & MRRoo::SF_SLOPED_FLOOR) != 0;
		Sec.bSlopedCeiling = (Sec.Flags & MRRoo::SF_SLOPED_CEILING) != 0;
		if (Sec.bSlopedFloor)
		{
			ReadSlope(C, Sec.FloorSlope);
		}
		if (Sec.bSlopedCeiling)
		{
			ReadSlope(C, Sec.CeilingSlope);
		}
	}

	if (!N.IsOk() || !W.IsOk() || !S.IsOk() || !C.IsOk())
	{
		OutError = FString::Printf(TEXT("cut short (nodes %d, walls %d, sidedefs %d, sectors %d)"), N.IsOk(), W.IsOk(), S.IsOk(), C.IsOk());
		return false;
	}
	return true;
}

TSet<uint16> FMRRooFile::TextureIds() const
{
	TSet<uint16> Out;
	for (const FMRRooSector& S : Sectors)
	{
		Out.Add(S.FloorTexture);
		Out.Add(S.CeilingTexture);
	}
	for (const FMRRooSidedef& S : Sidedefs)
	{
		Out.Add(S.NormalTexture);
		Out.Add(S.AboveTexture);
		Out.Add(S.BelowTexture);
	}
	Out.Remove(0);
	return Out;
}

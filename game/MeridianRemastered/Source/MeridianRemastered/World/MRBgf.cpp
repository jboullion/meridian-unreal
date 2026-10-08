#include "World/MRBgf.h"

#include "Engine/Texture2D.h"
#include "MeridianRemastered.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/MRProtocol.h"
#include "World/MRRooFile.h"
#include "Zones/MRZoneSubsystem.h"

bool FMRBgf::Load(const TArray<uint8>& Bytes, FString& OutError)
{
	const uint8 Magic[4] = {'B', 'G', 'F', 0x11};
	if (Bytes.Num() < 56 || FMemory::Memcmp(Bytes.GetData(), Magic, 4) != 0)
	{
		OutError = TEXT("not a BGF file");
		return false;
	}
	FMRReader R(Bytes.GetData(), Bytes.Num(), 4);
	const int32 Version = R.I32();
	if (Version < 10)
	{
		OutError = FString::Printf(TEXT("BGF version %d is unsupported"), Version);
		return false;
	}
	const char* NameBytes = reinterpret_cast<const char*>(Bytes.GetData() + 8);
	Name = FString(FCStringAnsi::Strnlen(NameBytes, 32), NameBytes);
	R.Skip(32);
	const int32 NumBitmaps = R.I32();
	const int32 NumGroups = R.I32();
	R.I32();  // max indices
	Shrink = FMath::Max(1, R.I32());
	if (NumBitmaps < 0 || NumBitmaps > 4096 || NumGroups < 0 || NumGroups > 4096)
	{
		OutError = TEXT("implausible header");
		return false;
	}
	Bitmaps.SetNum(NumBitmaps);
	for (FBitmap& B : Bitmaps)
	{
		B.Width = R.I32();
		B.Height = R.I32();
		B.XOffset = R.I32();
		B.YOffset = R.I32();
		B.Hotspots.SetNum(R.U8());
		for (FHotspot& H : B.Hotspots)
		{
			H.Num = static_cast<int8>(R.U8());
			H.X = R.I32();
			H.Y = R.I32();
		}
		const bool bCompressed = R.U8() != 0;
		const int32 Length = R.I32();
		const int32 Size = B.Width * B.Height;
		if (!R.IsOk() || B.Width <= 0 || B.Height <= 0 || Size > 4096 * 4096 || Length < 0 || Length > R.Remaining())
		{
			OutError = TEXT("cut short in a bitmap");
			return false;
		}
		const uint8* Src = Bytes.GetData() + (Bytes.Num() - R.Remaining());
		B.Pixels.SetNumUninitialized(Size);
		if (bCompressed)
		{
			if (!FCompression::UncompressMemory(NAME_Zlib, B.Pixels.GetData(), Size, Src, Length))
			{
				OutError = TEXT("a bitmap doesn't decompress");
				return false;
			}
			R.Skip(Length);
		}
		else
		{
			// uncompressed bitmaps are width * height bytes, whatever the length field says
			if (Size > R.Remaining())
			{
				OutError = TEXT("cut short in a bitmap");
				return false;
			}
			FMemory::Memcpy(B.Pixels.GetData(), Src, Size);
			R.Skip(Size);
		}
	}
	Groups.SetNum(NumGroups);
	for (TArray<int32>& G : Groups)
	{
		const int32 N = R.I32();
		if (N < 0 || N > 1024)
		{
			OutError = TEXT("implausible group");
			return false;
		}
		G.SetNum(N);
		for (int32& Index : G)
		{
			Index = R.I32();
		}
	}
	if (!R.IsOk())
	{
		OutError = TEXT("cut short in its groups");
		return false;
	}
	return true;
}

FIntPoint FMRBgf::TextureSize(int32 Bitmap) const
{
	const FBitmap* B = Bitmaps.IsValidIndex(Bitmap) ? &Bitmaps[Bitmap] : nullptr;
	return B ? FIntPoint(B->Height, B->Width) : FIntPoint(64, 64);
}

FVector2D FMRBgf::TextureRepeatRoo(int32 Bitmap) const
{
	const FIntPoint Size = TextureSize(Bitmap);
	return FVector2D(Size.X, Size.Y) / static_cast<double>(Shrink) * MRRoo::RooPerFine;
}

bool FMRBgf::HasTransparency(int32 Bitmap) const
{
	return Bitmaps.IsValidIndex(Bitmap) && Bitmaps[Bitmap].Pixels.Contains(Transparent);
}

const TArray<FColor>& FMRBgf::Palette()
{
	static TArray<FColor> Colors;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		TArray<uint8> Bytes;
		const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("runtime"), TEXT("palette.bin"));
		if (FFileHelper::LoadFileToArray(Bytes, *Path) && Bytes.Num() == 768)
		{
			Colors.SetNum(256);
			for (int32 i = 0; i < 256; ++i)
			{
				Colors[i] = FColor(Bytes[i * 3], Bytes[i * 3 + 1], Bytes[i * 3 + 2], i == Transparent ? 0 : 255);
			}
		}
		else
		{
			UE_LOG(LogMeridian, Error, TEXT("World: no palette at %s (tools/bgf2png/bgf2png.py --palette)"), *Path);
		}
	}
	return Colors;
}

UTexture2D* FMRBgf::MakeTexture(int32 Bitmap, bool bTransposed) const
{
	const TArray<FColor>& Pal = Palette();
	if (!Bitmaps.IsValidIndex(Bitmap) || Pal.Num() != 256)
	{
		return nullptr;
	}
	const FBitmap& B = Bitmaps[Bitmap];
	const int32 W = bTransposed ? B.Height : B.Width;
	const int32 H = bTransposed ? B.Width : B.Height;
	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, NAME_None);
	if (!Tex)
	{
		return nullptr;
	}
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	FColor* Dest = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			// transposed: the drawn (x, y) is the stored (y, x)
			const uint8 Index = bTransposed ? B.Pixels[X * B.Width + Y] : B.Pixels[Y * B.Width + X];
			Dest[Y * W + X] = Pal[Index];
		}
	}
	Mip.BulkData.Unlock();
	Tex->Filter = TF_Nearest;
	Tex->SRGB = true;
	Tex->AddressX = TA_Wrap;
	Tex->AddressY = TA_Wrap;
	Tex->NeverStream = true;
	Tex->UpdateResource();
	return Tex;
}

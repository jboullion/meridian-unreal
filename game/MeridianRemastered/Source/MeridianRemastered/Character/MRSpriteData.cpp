#include "Character/MRSpriteData.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MeridianRemastered.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	constexpr int32 NumDegrees = 4096;

	// FindHotspot results (clientd3d/object3d.h)
	enum EHotspotKind : int32 { HS_None, HS_Over, HS_Under, HS_OverUnder, HS_OverOver, HS_UnderUnder, HS_UnderOver, HS_OverUnderOverUnder };
	constexpr int32 HS_HELM = 2;
	constexpr int32 HS_RIGHT_HAND = 21;
	constexpr int32 HS_LEFT_HAND = 31;

	// draw order: underlay passes, the body, overlay passes (D3DRenderObjects + D3DRenderOverlaysDraw)
	int32 DepthRank(int32 Kind)
	{
		switch (Kind)
		{
		case HS_UnderUnder: return 0;
		case HS_Under: return 1;
		case HS_UnderOver: return 2;
		case -1: return 3;  // the body
		case HS_OverUnderOverUnder: return 4;
		case HS_OverUnder: return 5;
		case HS_Over: return 6;
		case HS_OverOver: return 7;
		default: return 8;
		}
	}

	int32 FindHotspot(const FMRSpriteBitmap& Bitmap, int32 Hotspot, FIntPoint& OutPos)
	{
		for (const TPair<int32, FIntPoint>& H : Bitmap.Hotspots)
		{
			if (FMath::Abs(H.Key) == Hotspot)
			{
				OutPos = H.Value;
				return H.Key > 0 ? (H.Key == HS_HELM ? HS_OverUnderOverUnder : HS_Over) : HS_Under;
			}
		}
		return HS_None;
	}

	// SendOverlays order (player.kod); the body is listed first in the JSON but drawn by rank
	const FName PartOrder[] = {TEXT("left_arm"), TEXT("right_arm"), TEXT("legs"), TEXT("head"), TEXT("mouth"),
		TEXT("eyes"), TEXT("nose"), TEXT("hair"), TEXT("weapon")};
	const FName BodyName(TEXT("body"));
}

// ------------------------------------------------------------------------------ bgf / look

int32 FMRSpriteBgf::BitmapIndex(int32 Group, int32 Angle) const
{
	if (!Groups.IsValidIndex(Group) || Groups[Group].Num() == 0)
	{
		return INDEX_NONE;
	}
	const TArray<int32>& G = Groups[Group];
	const int32 Index = G[FMRSpriteLibrary::ViewSlot(Angle, G.Num())];
	return Bitmaps.IsValidIndex(Index) ? Index : INDEX_NONE;
}

const FMRSpritePart* FMRSpriteLook::Find(FName Part) const
{
	return Parts.FindByPredicate([Part](const FMRSpritePart& P) { return P.Name == Part; });
}

int32 FMRSpriteAction::OnceLengthMs() const
{
	int32 Len = 0;
	for (const TPair<FName, FMRSpriteTrackDef>& T : Tracks)
	{
		if (T.Value.Mode == FMRSpriteTrackDef::EMode::Once)
		{
			Len = FMath::Max(Len, (T.Value.High - T.Value.Low + 1) * T.Value.PeriodMs);
		}
	}
	return Len;
}

// ------------------------------------------------------------------------------ colours

namespace
{
	const TCHAR* SkinNames[] = {TEXT("skin1"), TEXT("skin2"), TEXT("skin3"), TEXT("skin4")};
	// the character creator's hair list (player.kod CreateCharacter) and its xlat ids
	const TCHAR* HairNames[] = {TEXT("none"), TEXT("orange"), TEXT("red"), TEXT("skin1"), TEXT("skin2"), TEXT("skin3"),
		TEXT("skin4"), TEXT("skin5"), TEXT("platblond"), TEXT("korange"), TEXT("kred"), TEXT("kgray"), TEXT("black"), TEXT("blond")};
	const int32 HairXlats[] = {0x00, 0x0A, 0x12, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x30, 0x22, 0x2A, 0x2B, 0x2C, 0x2F};
	// XLAT_TO_* ramps (blakston.khd)
	const TCHAR* ClothesNames[] = {TEXT("red"), TEXT("skin1"), TEXT("skin2"), TEXT("skin4"), TEXT("orange"), TEXT("green"),
		TEXT("blue"), TEXT("purple"), TEXT("yellow"), TEXT("gray"), TEXT("sky")};
	// the clothes ramp a skin's hands and feet take (skin3 uses skin4's)
	const int32 SkinToClothes[] = {1, 2, 3, 3};
	constexpr int32 GuildBase = 0x87;
	constexpr int32 NumRamps = 11;
}

const TCHAR* FMRSpriteColours::SkinName(int32 Skin) { return SkinNames[FMath::Clamp(Skin, 0, NumSkins - 1)]; }
const TCHAR* FMRSpriteColours::HairName(int32 Hair) { return HairNames[FMath::Clamp(Hair, 0, NumHair - 1)]; }
const TCHAR* FMRSpriteColours::ClothesName(int32 Colour) { return ClothesNames[FMath::Clamp(Colour, 0, NumClothes - 1)]; }
int32 FMRSpriteColours::SkinXlat(int32 Skin) { return FMath::Clamp(Skin, 0, NumSkins - 1) + 1; }
int32 FMRSpriteColours::HairXlat(int32 Hair) { return HairXlats[FMath::Clamp(Hair, 0, NumHair - 1)]; }

int32 FMRSpriteColours::ClothesXlat(int32 Colour, int32 Skin)
{
	return GuildBase + FMath::Clamp(Colour, 0, NumClothes - 1) * NumRamps + SkinToClothes[FMath::Clamp(Skin, 0, NumSkins - 1)];
}

int32 FMRSpriteColours::ClothesOf(int32 Xlat)
{
	return Xlat >= GuildBase && Xlat < GuildBase + NumRamps * NumRamps ? (Xlat - GuildBase) / NumRamps : INDEX_NONE;
}

// ------------------------------------------------------------------------------ tracks

void FMRSpriteTrack::Start(const FMRSpriteTrackDef& InDef)
{
	Def = InDef;
	Group = Def.Low;
	TickMs = static_cast<float>(Def.PeriodMs);
}

bool FMRSpriteTrack::Step(float DtMs)
{
	if (Def.Mode == FMRSpriteTrackDef::EMode::None || Def.PeriodMs <= 0)
	{
		return false;
	}
	bool bChanged = false;
	TickMs -= DtMs;
	while (TickMs <= 0.f && Def.Mode != FMRSpriteTrackDef::EMode::None)
	{
		bChanged = true;
		if (Def.Mode == FMRSpriteTrackDef::EMode::Cycle)
		{
			Group = Def.Low + (Group - Def.Low + 1) % FMath::Max(1, Def.High - Def.Low + 1);
		}
		else if (Group >= Def.High)
		{
			Def.Mode = FMRSpriteTrackDef::EMode::None;
			Group = Def.Final;
			TickMs = 0.f;
			break;
		}
		else
		{
			++Group;
		}
		TickMs += Def.PeriodMs;
	}
	return bChanged;
}

int32 FMRSpriteTrack::NextGroup() const
{
	switch (Def.Mode)
	{
	case FMRSpriteTrackDef::EMode::Cycle:
		return Def.Low + (Group - Def.Low + 1) % FMath::Max(1, Def.High - Def.Low + 1);
	case FMRSpriteTrackDef::EMode::Once:
		return Group >= Def.High ? Def.Final : Group + 1;
	default:
		return Group;
	}
}

// ------------------------------------------------------------------------------ library

int32 FMRSpriteLibrary::ViewSlot(int32 Angle, int32 N)
{
	const int32 Interval = NumDegrees / FMath::Max(1, N) + 1;
	return FMath::Clamp(((Angle + Interval / 2) % NumDegrees) / Interval, 0, N - 1);
}

int32 FMRSpriteLibrary::RelativeAngle(float FacingYawDeg, float YawToViewerDeg)
{
	// d3drender.c: angle = (object angle - direction from the object to the viewer). UE yaw and the
	// client's angles both turn clockwise seen from above.
	const float Deg = FRotator::NormalizeAxis(FacingYawDeg - YawToViewerDeg);
	const int32 A = FMath::RoundToInt(Deg * NumDegrees / 360.f);
	return ((A % NumDegrees) + NumDegrees) % NumDegrees;
}

bool FMRSpriteLibrary::Place(const FMRSpriteLook& Look, const TMap<FName, int32>& Groups, int32 Angle,
	TArray<FMRSpritePlaced>& Out, FVector2f& OutFeet, int32& OutShrink, const TMap<FName, int32>* BitmapOverride,
	bool bBackArmsUnder) const
{
	Out.Reset();
	const FMRSpritePart* Body = Look.Find(BodyName);
	const FMRSpriteBgf* BodyBgf = Body ? FindBgf(Body->Bgf) : nullptr;
	if (!BodyBgf)
	{
		return false;
	}
	auto GroupOf = [&Groups](FName Part) { const int32* G = Groups.Find(Part); return G ? *G : 0; };
	// the bitmap a part shows: an override (an in-between) or its group at this angle
	auto Resolve = [&](FName Part, const FMRSpriteBgf* Bgf) -> int32
	{
		if (const int32* O = BitmapOverride ? BitmapOverride->Find(Part) : nullptr)
		{
			return Bgf->Bitmaps.IsValidIndex(*O) ? *O : INDEX_NONE;
		}
		return Bgf->BitmapIndex(GroupOf(Part), Angle);
	};
	const int32 Bi = Resolve(BodyName, BodyBgf);
	if (Bi == INDEX_NONE)
	{
		return false;
	}
	const FMRSpriteBitmap& Tb = BodyBgf->Bitmaps[Bi];
	const float SBase = static_cast<float>(BodyBgf->Shrink);
	OutShrink = BodyBgf->Shrink;
	const float FinePerPx = 16.f / SBase;
	OutFeet = FVector2f(Tb.W * 0.5f - Tb.XOff / FinePerPx, Tb.H - Tb.YOff * 4.f / FinePerPx);

	// a hotspot on the torso; seen from behind, the arms go under it (see the header)
	const int32 Slot = ViewSlot(Angle, 8);
	const bool bFromBehind = bBackArmsUnder && Slot >= 3 && Slot <= 5;
	auto TorsoHotspot = [&Tb, bFromBehind](int32 Hotspot, FIntPoint& OutPos)
	{
		const int32 Kind = FindHotspot(Tb, Hotspot, OutPos);
		return bFromBehind && Kind == HS_Over && (Hotspot == HS_RIGHT_HAND || Hotspot == HS_LEFT_HAND) ? HS_Under : Kind;
	};

	struct FEntry { FMRSpritePlaced P; int32 Rank; int32 Order; };
	TArray<FEntry> Entries;
	Entries.Add({FMRSpritePlaced{BodyName, Body, Bi, FVector2f::ZeroVector, 1.f, -1}, DepthRank(-1), -1});

	for (int32 Order = 0; Order < Look.Parts.Num(); ++Order)
	{
		const FMRSpritePart& Ov = Look.Parts[Order];
		if (Ov.Name == BodyName || Ov.Hotspot == 0)
		{
			continue;
		}
		const FMRSpriteBgf* Ob = FindBgf(Ov.Bgf);
		const int32 Oi = Ob ? Resolve(Ov.Name, Ob) : INDEX_NONE;
		if (Oi == INDEX_NONE)
		{
			continue;
		}
		const FMRSpriteBitmap& Bm = Ob->Bitmaps[Oi];
		FIntPoint H;
		int32 Kind = TorsoHotspot(Ov.Hotspot, H);
		FVector2f Pos = FVector2f::ZeroVector;
		if (Kind != HS_None)
		{
			// the overlay's offset counts in base pixels (the original's quirk)
			Pos = FVector2f(H.X + Bm.XOff, H.Y + Bm.YOff);
		}
		else
		{
			// an overlay on an overlay (face parts and hair on the head)
			bool bFound = false;
			for (const FMRSpritePart& BaseOv : Look.Parts)
			{
				if (&BaseOv == &Ov || BaseOv.Name == BodyName || BaseOv.Hotspot == 0)
				{
					continue;
				}
				const FMRSpriteBgf* B2 = FindBgf(BaseOv.Bgf);
				const int32 B2i = B2 ? Resolve(BaseOv.Name, B2) : INDEX_NONE;
				if (B2i == INDEX_NONE)
				{
					continue;
				}
				FIntPoint H2, H1;
				const int32 K2 = FindHotspot(B2->Bitmaps[B2i], Ov.Hotspot, H2);
				if (K2 == HS_None)
				{
					continue;
				}
				const int32 K1 = TorsoHotspot(BaseOv.Hotspot, H1);
				if (K1 == HS_None)
				{
					continue;
				}
				const FMRSpriteBitmap& Bm2 = B2->Bitmaps[B2i];
				const float R = SBase / B2->Shrink;
				Pos = FVector2f(H1.X + Bm2.XOff + (H2.X + Bm.XOff) * R, H1.Y + Bm2.YOff + (H2.Y + Bm.YOff) * R);
				Kind = K1 == HS_Over ? (K2 == HS_Over ? HS_OverOver : HS_OverUnder) : (K2 == HS_Over ? HS_UnderOver : HS_UnderUnder);
				bFound = true;
				break;
			}
			if (!bFound)
			{
				continue;
			}
		}
		Entries.Add({FMRSpritePlaced{Ov.Name, &Ov, Oi, Pos, SBase / Ob->Shrink, Kind}, DepthRank(Kind), Order});
	}
	Entries.StableSort([](const FEntry& A, const FEntry& B) { return A.Rank != B.Rank ? A.Rank < B.Rank : A.Order < B.Order; });
	for (const FEntry& E : Entries)
	{
		Out.Add(E.P);
	}
	return true;
}

// ------------------------------------------------------------------------------ loading

const FMRSpriteLibrary& FMRSpriteLibrary::Get()
{
	static FMRSpriteLibrary Library;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		const FString Dir = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("sprites"));
		FString PartsText, ActionsText;
		if (FFileHelper::LoadFileToString(PartsText, *FPaths::Combine(Dir, TEXT("player_parts.json")))
			&& FFileHelper::LoadFileToString(ActionsText, *FPaths::Combine(Dir, TEXT("player_actions.json")))
			&& Library.LoadFromStrings(PartsText, ActionsText))
		{
			UE_LOG(LogMeridian, Log, TEXT("Sprite data: %d bgfs, %d atlases, %d looks, %d actions"),
				Library.Bgfs.Num(), Library.Atlases.Num(), Library.Looks.Num(), Library.Actions.Num());
		}
		else
		{
			UE_LOG(LogMeridian, Warning, TEXT("Sprite data missing in %s (run tools/sprites/build_player_sprites.py)"), *Dir);
		}
	}
	return Library;
}

bool FMRSpriteLibrary::LoadFromStrings(const FString& PartsJson, const FString& ActionsJson)
{
	TSharedPtr<FJsonObject> PartsRoot, ActionsRoot;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PartsJson), PartsRoot) || !PartsRoot
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ActionsJson), ActionsRoot) || !ActionsRoot)
	{
		return false;
	}
	return ParseParts(PartsRoot) && ParseActions(ActionsRoot);
}

bool FMRSpriteLibrary::ParseParts(const TSharedPtr<FJsonObject>& Root)
{
	Root->TryGetStringField(TEXT("texture_dir"), TextureDir);
	Root->TryGetNumberField(TEXT("scale"), AtlasScale);
	Root->TryGetNumberField(TEXT("square_cm"), SquareCm);
	Root->TryGetNumberField(TEXT("fine_per_square"), FinePerSquare);

	const TSharedPtr<FJsonObject>* BgfsObj;
	if (!Root->TryGetObjectField(TEXT("bgfs"), BgfsObj))
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*BgfsObj)->Values)
	{
		const TSharedPtr<FJsonObject> O = It.Value->AsObject();
		FMRSpriteBgf& B = Bgfs.Add(It.Key);
		B.Shrink = FMath::Max(1, static_cast<int32>(O->GetNumberField(TEXT("shrink"))));
		for (const TSharedPtr<FJsonValue>& G : O->GetArrayField(TEXT("groups")))
		{
			TArray<int32>& Group = B.Groups.AddDefaulted_GetRef();
			for (const TSharedPtr<FJsonValue>& I : G->AsArray())
			{
				Group.Add(static_cast<int32>(I->AsNumber()));
			}
		}
		const TSharedPtr<FJsonObject>* TweenObj;
		if (O->TryGetObjectField(TEXT("tweens"), TweenObj))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& T : (*TweenObj)->Values)
			{
				FString A, Bs;
				if (!T.Key.Split(TEXT(">"), &A, &Bs))
				{
					continue;
				}
				TArray<int32>& List = B.Tweens.Add((uint64(uint32(FCString::Atoi(*A))) << 32) | uint32(FCString::Atoi(*Bs)));
				for (const TSharedPtr<FJsonValue>& I : T.Value->AsArray())
				{
					List.Add(static_cast<int32>(I->AsNumber()));
				}
			}
		}
		for (const TSharedPtr<FJsonValue>& V : O->GetArrayField(TEXT("bitmaps")))
		{
			const TSharedPtr<FJsonObject> BO = V->AsObject();
			FMRSpriteBitmap& Bm = B.Bitmaps.AddDefaulted_GetRef();
			Bm.W = BO->GetIntegerField(TEXT("w"));
			Bm.H = BO->GetIntegerField(TEXT("h"));
			Bm.XOff = BO->GetIntegerField(TEXT("xoff"));
			Bm.YOff = BO->GetIntegerField(TEXT("yoff"));
			for (const TSharedPtr<FJsonValue>& HV : BO->GetArrayField(TEXT("hotspots")))
			{
				const TArray<TSharedPtr<FJsonValue>>& H = HV->AsArray();
				Bm.Hotspots.Add({static_cast<int32>(H[0]->AsNumber()), FIntPoint(H[1]->AsNumber(), H[2]->AsNumber())});
			}
		}
	}

	const TSharedPtr<FJsonObject>* AtlasObj;
	if (Root->TryGetObjectField(TEXT("atlases"), AtlasObj))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*AtlasObj)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			FMRSpriteAtlas& A = Atlases.Add(It.Key);
			A.Texture = O->GetStringField(TEXT("texture"));
			O->TryGetStringField(TEXT("ramp"), A.RampTexture);
			A.Size = FIntPoint(O->GetIntegerField(TEXT("w")), O->GetIntegerField(TEXT("h")));
			for (const TPair<FString, TSharedPtr<FJsonValue>>& C : O->GetObjectField(TEXT("cells"))->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>& R = C.Value->AsArray();
				const FIntPoint Min(R[0]->AsNumber(), R[1]->AsNumber());
				A.Cells.Add(FCString::Atoi(*C.Key), FIntRect(Min, Min + FIntPoint(R[2]->AsNumber(), R[3]->AsNumber())));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Classes;
	if (Root->TryGetArrayField(TEXT("material_classes"), Classes))
	{
		for (const TSharedPtr<FJsonValue>& V : *Classes)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMRSpriteMaterialClass& C = MaterialClasses.AddDefaulted_GetRef();
			O->TryGetStringField(TEXT("name"), C.Name);
			O->TryGetNumberField(TEXT("roughness"), C.Roughness);
			O->TryGetNumberField(TEXT("metallic"), C.Metallic);
			O->TryGetNumberField(TEXT("specular"), C.Specular);
		}
	}

	const TSharedPtr<FJsonObject>* LooksObj;
	if (Root->TryGetObjectField(TEXT("looks"), LooksObj))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*LooksObj)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			FMRSpriteLook& L = Looks.Add(FName(*It.Key));
			L.Name = FName(*It.Key);
			O->TryGetNumberField(TEXT("action_face"), L.ActionFace);
			const TArray<TSharedPtr<FJsonValue>>* Bounds;
			if (O->TryGetArrayField(TEXT("bounds"), Bounds) && Bounds->Num() == 4)
			{
				L.Bounds = FBox2f(FVector2f((*Bounds)[0]->AsNumber(), (*Bounds)[1]->AsNumber()),
					FVector2f((*Bounds)[2]->AsNumber(), (*Bounds)[3]->AsNumber()));
			}
			const TSharedPtr<FJsonObject> P = O->GetObjectField(TEXT("parts"));
			auto AddPart = [this, &L, &P](FName Name)
			{
				const TSharedPtr<FJsonObject>* PO;
				if (!P->TryGetObjectField(Name.ToString(), PO))
				{
					return;
				}
				FMRSpritePart& Part = L.Parts.AddDefaulted_GetRef();
				Part.Name = Name;
				Part.Bgf = (*PO)->GetStringField(TEXT("bgf"));
				Part.Hotspot = (*PO)->GetIntegerField(TEXT("hotspot"));
				Part.Atlas = Part.Bgf;
				Part.Xlat = (*PO)->GetIntegerField(TEXT("xlat"));
				(*PO)->TryGetNumberField(TEXT("class"), Part.Class);
			};
			for (const FName& Name : PartOrder)
			{
				AddPart(Name);
			}
			AddPart(BodyName);
			const TSharedPtr<FJsonObject>* Acts;
			if (O->TryGetObjectField(TEXT("actions"), Acts))
			{
				TMap<FName, FMRSpriteAction>& Own = LookActions.Add(L.Name);
				for (const TPair<FString, TSharedPtr<FJsonValue>>& A : (*Acts)->Values)
				{
					ParseAction(FName(*A.Key), A.Value->AsObject(), Own.Add(FName(*A.Key)));
				}
			}
		}
	}

	const TSharedPtr<FJsonObject>* MonstersObj;
	if (Root->TryGetObjectField(TEXT("monsters"), MonstersObj))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*MonstersObj)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			FMRMonsterDef& M = Monsters.Add(FName(*It.Key));
			M.Class = FName(*It.Key);
			O->TryGetStringField(TEXT("name"), M.Name);
			FString S;
			if (O->TryGetStringField(TEXT("look"), S)) { M.Look = FName(*S); }
			if (O->TryGetStringField(TEXT("dead_look"), S)) { M.DeadLook = FName(*S); }
			O->TryGetNumberField(TEXT("speed_cms"), M.SpeedCms);
			O->TryGetNumberField(TEXT("vision_cm"), M.VisionCm);
			O->TryGetBoolField(TEXT("aggressive"), M.bAggressive);
			O->TryGetBoolField(TEXT("npc"), M.bNpc);
			O->TryGetBoolField(TEXT("stationary"), M.bStationary);
			const TSharedPtr<FJsonObject>* Sounds;
			if (O->TryGetObjectField(TEXT("sounds"), Sounds))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Snd : (*Sounds)->Values)
				{
					M.Sounds.Add(FName(*Snd.Key), Snd.Value->AsString());
				}
			}
		}
	}
	return Bgfs.Num() > 0;
}

FMRSpriteTrackDef FMRSpriteLibrary::ParseTrack(const TSharedPtr<FJsonObject>& O)
{
	FMRSpriteTrackDef D;
	const FString Mode = O->GetStringField(TEXT("mode"));
	D.Mode = Mode == TEXT("cycle") ? FMRSpriteTrackDef::EMode::Cycle
		: Mode == TEXT("once") ? FMRSpriteTrackDef::EMode::Once : FMRSpriteTrackDef::EMode::None;
	O->TryGetNumberField(TEXT("period"), D.PeriodMs);
	O->TryGetNumberField(TEXT("low"), D.Low);
	D.High = D.Low;
	O->TryGetNumberField(TEXT("high"), D.High);
	O->TryGetNumberField(TEXT("final"), D.Final);
	return D;
}

bool FMRSpriteLibrary::ParseActions(const TSharedPtr<FJsonObject>& Root)
{
	const TSharedPtr<FJsonObject>* Fp;
	if (Root->TryGetObjectField(TEXT("_first_person"), Fp))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*Fp)->Values)
		{
			if (It.Key.StartsWith(TEXT("_")) || It.Value->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			FMRFirstPersonOverlay& F = FirstPerson.Add(It.Key);
			F.Bgf = O->GetStringField(TEXT("bgf"));
			O->TryGetNumberField(TEXT("hold"), F.Hold);
			const TSharedPtr<FJsonObject>* Attack;
			if (O->TryGetObjectField(TEXT("attack"), Attack))
			{
				F.Attack = ParseTrack(*Attack);
			}
		}
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& It : Root->Values)
	{
		if (It.Key.StartsWith(TEXT("_")) || It.Value->Type != EJson::Object)
		{
			continue;
		}
		FMRSpriteAction& A = Actions.Add(FName(*It.Key));
		ParseAction(FName(*It.Key), It.Value->AsObject(), A);
	}
	return Actions.Num() > 0;
}

void FMRSpriteLibrary::ParseAction(FName Name, const TSharedPtr<FJsonObject>& O, FMRSpriteAction& A)
{
	A.Name = Name;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& T : O->Values)
	{
		FMRSpriteTrackDef& D = A.Tracks.Add(FName(*T.Key));
		if (T.Value->Type == EJson::Number)
		{
			D.Low = D.High = D.Final = static_cast<int32>(T.Value->AsNumber());
			continue;
		}
		D = ParseTrack(T.Value->AsObject());
	}
}

const FMRSpriteAction* FMRSpriteLibrary::FindAction(FName Look, FName Action) const
{
	if (const TMap<FName, FMRSpriteAction>* Own = LookActions.Find(Look))
	{
		if (const FMRSpriteAction* A = Own->Find(Action))
		{
			return A;
		}
		if (Action != TEXT("stand"))
		{
			return nullptr;  // a monster has only its own actions (it may have no walk, no attack)
		}
	}
	return Actions.Find(Action);
}

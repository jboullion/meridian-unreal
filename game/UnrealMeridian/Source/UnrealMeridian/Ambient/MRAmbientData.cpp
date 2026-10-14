#include "Ambient/MRAmbientData.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Net/MRNetLook.h"
#include "Net/MRNetWorld.h"
#include "Net/MRProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMeridian.h"
#include "Zones/MRZoneSubsystem.h"

namespace MRAmbient
{
	namespace
	{
		/** blakston.khd XLAT_TO_*: a two-colour translation's colours (townsfolk.ts C) */
		enum EColour : int32 { RED = 0, SKIN1 = 1, SKIN2 = 2, SKIN4 = 3, ORANGE = 4, GREEN = 5, BLUE = 6, PURPLE = 7, YELLOW = 8, GRAY = 9, SKY = 10, DGREEN = 11, BLACK = 12 };

		template <typename T>
		struct TW
		{
			T V;
			double W;
		};

		template <typename T>
		T Weighted(FRand& Rand, const TArray<TW<T>>& List)
		{
			double Total = 0.0;
			for (const TW<T>& E : List)
			{
				Total += E.W;
			}
			double R = Rand() * Total;
			for (const TW<T>& E : List)
			{
				if ((R -= E.W) < 0.0)
				{
					return E.V;
				}
			}
			return List.Last().V;
		}

		template <typename T>
		const T& Pick(FRand& Rand, const TArray<T>& List)
		{
			return List[Rand.Index(List.Num())];
		}

		/** system.kod SendCharInfo: the face's skin translation (PT_BLUE_TO_SKIN1..4) and the body's skin colour for it */
		const TArray<TPair<int32, int32>> Skins = {{0x01, SKIN1}, {0x02, SKIN1}, {0x03, SKIN2}, {0x04, SKIN4}};
		/** system.kod SendCharInfo's hair colours, natural ones more often */
		const TArray<TW<int32>> HairColours = {{0x2c, 4}, {0x22, 4}, {0x1d, 3}, {0x1e, 3}, {0x2f, 3}, {0x30, 2}, {0x2a, 2}, {0x2b, 2},
			{0x1a, 1}, {0x1b, 1}, {0x1c, 1}, {0x0a, 1}, {0x12, 1}};

		struct FFace
		{
			FString Head;
			TArray<TW<FString>> Hair;
			TArray<FString> Eyes, Noses, Mouths;
		};
		/** system.kod GetAllowed*Icons (no mohawks in town, mostly); "" is bald */
		const FFace Faces[2] = {
			{TEXT("phax"), {{TEXT("ptad"), 4}, {TEXT("ptbb"), 3}, {TEXT("ptac"), 2}, {TEXT("ptba"), 1}, {TEXT("ptxa"), 2}, {TEXT(""), 2}, {TEXT("ptcd"), 0.3}},
				{TEXT("peax"), TEXT("pebx"), TEXT("pecx"), TEXT("pedx")}, {TEXT("pnax"), TEXT("pnbx"), TEXT("pncx")}, {TEXT("pmax"), TEXT("pmbx"), TEXT("pmcx")}},
			{TEXT("phkx"), {{TEXT("ptca"), 3}, {TEXT("ptbc"), 3}, {TEXT("ptdc"), 3}, {TEXT("ptcb"), 3}, {TEXT("ptbd"), 2}, {TEXT("ptdb"), 2}, {TEXT("ptxb"), 2}, {TEXT("ptdr"), 1}, {TEXT("ptcd"), 0.2}},
				{TEXT("pekx"), TEXT("pelx"), TEXT("pemx")}, {TEXT("pnkx"), TEXT("pnlx"), TEXT("pnmx")}, {TEXT("pmkx"), TEXT("pmlx"), TEXT("pmmx")}},
		};

		enum class ETop : uint8 { Shirt, Bare, Royal, Robe };
		struct FTop
		{
			double Weight[2];
			const TCHAR* Body[2];
			const TCHAR* Arms[2][2];  // [sex][left, right]
			ETop Kind;
		};
		/**
		 * The clothing item classes' player art: shirts (shirtbase.kod), the light jerkin (tanktop.kod), the
		 * royal shirt, leather (armour takes the shirt's colours: armor.kod SetPaletteTranslation) and robes
		 */
		const FTop Tops[] = {
			{{5, 5}, {TEXT("bta"), TEXT("btb")}, {{TEXT("bla"), TEXT("bra")}, {TEXT("blb"), TEXT("brb")}}, ETop::Shirt},
			{{2, 1.5}, {TEXT("bta"), TEXT("btb")}, {{TEXT("bla"), TEXT("bra")}, {TEXT("blb"), TEXT("brb")}}, ETop::Bare},
			{{1, 1}, {TEXT("bta"), TEXT("btb")}, {{TEXT("bla"), TEXT("bra")}, {TEXT("blb"), TEXT("brb")}}, ETop::Royal},
			{{1, 0.6}, {TEXT("btg"), TEXT("bth")}, {{TEXT("bla"), TEXT("bra")}, {TEXT("blb"), TEXT("brb")}}, ETop::Shirt},
			{{3, 3}, {TEXT("btc"), TEXT("btd")}, {{TEXT("blc"), TEXT("brc")}, {TEXT("bld"), TEXT("brd")}}, ETop::Robe},
		};
		struct FBottom
		{
			double Weight[2];
			const TCHAR* Legs[2];
			bool bSkirt;
		};
		/** Baggy trousers (pantsc/d.kod), skirts (skirt.kod, shrtskrt.kod), and the plain legs only some wear */
		const FBottom Bottoms[] = {
			{{6, 3}, {TEXT("bfe"), TEXT("bff")}, false},
			{{0.2, 3}, {TEXT("bfc"), TEXT("bfd")}, true},
			{{0.1, 1.2}, {TEXT("bfg"), TEXT("bfh")}, true},
			{{1.5, 1}, {TEXT("bfa"), TEXT("bfb")}, false},
		};
		const TCHAR* RobeLegs[2] = {TEXT("bfc"), TEXT("bfd")};
		const TCHAR* ShirtBody[2] = {TEXT("bta"), TEXT("btb")};
		const TCHAR* ShirtArms[2][2] = {{TEXT("bla"), TEXT("bra")}, {TEXT("blb"), TEXT("brb")}};
		const TCHAR* RobeBody[2] = {TEXT("btc"), TEXT("btd")};
		const TCHAR* RobeArms[2][2] = {{TEXT("blc"), TEXT("brc")}, {TEXT("bld"), TEXT("brd")}};
		const TCHAR* GauntletArms[2][2] = {{TEXT("ble"), TEXT("bre")}, {TEXT("blf"), TEXT("brf")}};
		const TCHAR* Pants[2] = {TEXT("bfe"), TEXT("bff")};
		const TCHAR* PlainLegs[2] = {TEXT("bfa"), TEXT("bfb")};

		/** Shirts are brighter (no skin tones: they'd look bare), trousers mostly browns and greys */
		const TArray<TW<int32>> ShirtColours = {{RED, 2}, {ORANGE, 0.5}, {GREEN, 2}, {BLUE, 2}, {PURPLE, 1}, {YELLOW, 1}, {GRAY, 2}, {SKY, 1}, {DGREEN, 2}, {BLACK, 1}};
		/** A royal shirt's two colours: no dark green or black, which only pair with skin */
		const TArray<TW<int32>> RoyalColours = {{RED, 2}, {GREEN, 1}, {BLUE, 2}, {PURPLE, 2}, {YELLOW, 2}, {GRAY, 1}, {SKY, 1}};
		const TArray<TW<int32>> PantsColours = {{SKIN1, 3}, {SKIN2, 3}, {SKIN4, 2}, {GRAY, 2}, {BLACK, 2}, {BLUE, 1}, {DGREEN, 1}};
		const TArray<TW<int32>> SkirtColours = {{SKIN1, 3}, {SKIN2, 3}, {SKIN4, 2}, {GRAY, 2}, {BLACK, 2}, {BLUE, 1}, {DGREEN, 1}, {RED, 1}, {PURPLE, 1}, {GREEN, 1}, {SKY, 0.5}};
		const TArray<TW<int32>> RobeColours = {{BLUE, 2}, {RED, 1}, {PURPLE, 2}, {DGREEN, 2}, {GRAY, 2}, {BLACK, 1}, {SKIN1, 2}, {SKY, 1}, {YELLOW, 0.5}};
		const TArray<TW<int32>> DarkPants = {{BLACK, 3}, {SKIN4, 2}, {GRAY, 2}, {SKIN2, 1}};

		/** Armour's player art (armor/*.kod) [male, female] */
		const TMap<FString, TPair<FString, FString>> Armour = {
			{TEXT("leather"), {TEXT("btg"), TEXT("bth")}}, {TEXT("chain"), {TEXT("btk"), TEXT("btl")}}, {TEXT("scale"), {TEXT("bti"), TEXT("btj")}},
			{TEXT("plate"), {TEXT("bte"), TEXT("btf")}}, {TEXT("nerudite"), {TEXT("btm"), TEXT("btn")}}};
		/**
		 * Helmets (helmet/*.kod): every one hides the hair (each class sends RemoveHair). Shards draws the
		 * female art (helmb...); our sprite build has each helmet's one art for both (equipment.json), so both wear it.
		 */
		const TMap<FString, FString> Helms = {{TEXT("simple"), TEXT("helm")}, {TEXT("war"), TEXT("warhelm")}, {TEXT("knight"), TEXT("khelm")},
			{TEXT("ornate"), TEXT("ornhelma")}, {TEXT("dark"), TEXT("dhelma")}, {TEXT("legion"), TEXT("legionhelm")}};
		/** Circlets (circlet.kod, ivycircl.kod, thorncirclet.kod), over the hair: their worn art as equipment.json names it */
		const TArray<FString> Circlets = {TEXT("invcirc"), TEXT("ivycircl"), TEXT("thocircl")};
		/** Weapons' overlays in the hand (weapon/*.kod) */
		const TArray<FString> Weapons = {TEXT("swordov"), TEXT("shswdov"), TEXT("scimov"), TEXT("maceov"), TEXT("hamrov"), TEXT("axeov")};
		/** Shields: metal, knight's, gold, nerudite */
		const TArray<FString> Shields = {TEXT("metlshld"), TEXT("mtlshld"), TEXT("rshield"), TEXT("nerushld")};
		constexpr const TCHAR* TorchOverlay = TEXT("torchov");

		/** blakston.khd HS_* */
		constexpr uint8 HsHead = 1, HsEyes = 11, HsMouth = 12, HsToupee = 13, HsNose = 14, HsRightHand = 21, HsRightWeapon = 22, HsLeftHand = 31, HsLeftWeapon = 32, HsLegs = 41;

		struct FPerson
		{
			bool bFemale;
			int32 Sex;
			int32 SkinXlat, Skin;
		};

		/** A face, hair and skin, as anyone's */
		FPerson Person(FRand& Rand, bool bFemale, FLook& L)
		{
			const FFace& Face = Faces[bFemale ? 1 : 0];
			const TPair<int32, int32>& S = Pick(Rand, Skins);
			L.bFemale = bFemale;
			L.Head = Face.Head;
			L.Eyes = Pick(Rand, Face.Eyes);
			L.Nose = Pick(Rand, Face.Noses);
			L.Mouth = Pick(Rand, Face.Mouths);
			L.Hair = Weighted(Rand, Face.Hair);
			L.HairXlat = Weighted(Rand, HairColours);
			L.SkinXlat = S.Key;
			return FPerson{bFemale, bFemale ? 1 : 0, S.Key, S.Value};
		}

		TArray<TW<int32>> Without(const TArray<TW<int32>>& List, int32 V)
		{
			return List.FilterByPredicate([V](const TW<int32>& E) { return E.V != V; });
		}

		FMRNetOverlay Overlay(const FString& Bgf, uint8 Hotspot, int32 Xlat, uint8 Type, uint16 Group, uint32 Period = 0, uint16 High = 0)
		{
			FMRNetOverlay O;
			O.Bgf = Bgf;
			O.Hotspot = Hotspot;
			O.Xlat = Xlat;
			O.Animation.Type = Type;
			O.Animation.Group = Group;
			O.Animation.GroupLow = Group;
			O.Animation.GroupHigh = High ? High : Group;
			O.Animation.Period = Period;
			return O;
		}

		TArray<TW<FString>> Strings(std::initializer_list<TW<FString>> L)
		{
			return TArray<TW<FString>>(L);
		}
	}

	// ------------------------------------------------------------------ data

	const FData& FData::Get()
	{
		static FData Data;
		if (!Data.bLoaded)
		{
			Data.Load();
		}
		return Data;
	}

	void FData::Reload()
	{
		const_cast<FData&>(Get()).Load();
	}

	void FData::Load()
	{
		Rooms.Reset();
		Busy.Reset();
		ErrandRooms.Reset();
		Chatter.Reset();
		ByPlace.Reset();
		ByRole.Reset();
		Names.Reset();
		bLoaded = true;  // tried: a missing file leaves no crowds, not a retry each frame
		const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ambient"), TEXT("ambient.json"));
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRAmbient: no %s (tools/world/ambient_data.py): no ambient life"), *Path);
			return;
		}
		const auto StringList = [](const TArray<TSharedPtr<FJsonValue>>& Values)
		{
			TArray<FString> Out;
			for (const TSharedPtr<FJsonValue>& V : Values)
			{
				Out.Add(V->AsString());
			}
			return Out;
		};
		const auto ListsOf = [&StringList](const TSharedPtr<FJsonObject>& Obj, TMap<FString, TArray<FString>>& Into)
		{
			if (Obj.IsValid())
			{
				for (const auto& Pair : Obj->Values)
				{
					Into.Add(FString(Pair.Key), StringList(Pair.Value->AsArray()));
				}
			}
		};
		if (const TSharedPtr<FJsonObject>* R = nullptr; Root->TryGetObjectField(TEXT("rooms"), R))
		{
			for (const auto& Pair : (*R)->Values)
			{
				const TSharedPtr<FJsonObject> O = Pair.Value->AsObject();
				FRoomSpec S;
				const FString Kind = O->GetStringField(TEXT("kind"));
				S.Kind = Kind == TEXT("inn") ? EKind::Inn : Kind == TEXT("castle") ? EKind::Castle : Kind == TEXT("arena") ? EKind::Arena
					: Kind == TEXT("shop") ? EKind::Shop : EKind::Town;
				S.Count = O->GetIntegerField(TEXT("count"));
				O->TryGetStringField(TEXT("place"), S.Place);
				O->TryGetNumberField(TEXT("guards"), S.Guards);
				O->TryGetNumberField(TEXT("livery"), S.Livery);
				O->TryGetStringField(TEXT("faction"), S.Faction);
				O->TryGetBoolField(TEXT("arena"), S.bArena);
				Rooms.Add(FString(Pair.Key).ToLower(), S);
			}
		}
		if (const TSharedPtr<FJsonObject>* B = nullptr; Root->TryGetObjectField(TEXT("busy_by_hour"), B))
		{
			for (const auto& Pair : (*B)->Values)
			{
				TArray<double>& Hours = Busy.Add(FString(Pair.Key));
				for (const TSharedPtr<FJsonValue>& V : Pair.Value->AsArray())
				{
					Hours.Add(V->AsNumber());
				}
			}
		}
		if (const TArray<TSharedPtr<FJsonValue>>* E = nullptr; Root->TryGetArrayField(TEXT("errand_rooms"), E))
		{
			ErrandRooms = StringList(*E);
		}
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		ListsOf(Root->TryGetObjectField(TEXT("chatter"), Obj) ? *Obj : nullptr, Chatter);
		ListsOf(Root->TryGetObjectField(TEXT("chatter_by_place"), Obj) ? *Obj : nullptr, ByPlace);
		ListsOf(Root->TryGetObjectField(TEXT("chatter_by_role"), Obj) ? *Obj : nullptr, ByRole);
		ListsOf(Root->TryGetObjectField(TEXT("names"), Obj) ? *Obj : nullptr, Names);
		UE_LOG(LogMeridian, Log, TEXT("MRAmbient: %d crowd rooms, %d chatter pools"), Rooms.Num(), Chatter.Num());
	}

	double FData::BusyAt(EKind Kind, double Hour) const
	{
		if (Hour < 0.0)
		{
			return 1.0;
		}
		const TCHAR* Table = Kind == EKind::Town || Kind == EKind::Shop ? TEXT("town") : Kind == EKind::Inn ? TEXT("inn")
			: Kind == EKind::Arena ? TEXT("arena") : TEXT("court");
		const TArray<double>* Hours = Busy.Find(Table);
		if (!Hours || Hours->Num() != 24)
		{
			return 1.0;
		}
		return (*Hours)[((FMath::FloorToInt(Hour) % 24) + 24) % 24];
	}

	bool FData::IsErrandRoom(const FString& RoomFile) const
	{
		// ambientRooms.ts isErrandRoom: a smith's, bank's, vault's, shop's... door
		const FString F = RoomFile.ToLower();
		for (const FString& E : ErrandRooms)
		{
			if (F.EndsWith(E + TEXT(".roo")))
			{
				return true;
			}
		}
		return false;
	}

	const TArray<FString>& FData::Lines(const TCHAR* Pool) const
	{
		static const TArray<FString> None = {TEXT("...")};
		const TArray<FString>* L = Chatter.Find(Pool);
		return L && L->Num() ? *L : None;
	}

	FString FData::ChatterLine(FRand& Rand, const FChatter& Ctx) const
	{
		// chatter.ts chatterLine
		const bool bNight = Ctx.Hour >= 0.0 && (Ctx.Hour >= 21.0 || Ctx.Hour < 5.0);
		if (!Ctx.FanOf.IsEmpty())
		{
			const TArray<FString>& L = Lines(Ctx.FanOutcome > 0 ? TEXT("fan_won") : Ctx.FanOutcome < 0 ? TEXT("fan_lost") : TEXT("fan_cheer"));
			return Pick(Rand, L).Replace(TEXT("{name}"), *Ctx.FanOf);
		}
		if (Ctx.bWon) return Pick(Rand, Lines(TEXT("won")));
		if (Ctx.bLost) return Pick(Rand, Lines(TEXT("lost")));
		if (Ctx.bBlinded) return Pick(Rand, Lines(TEXT("blinded")));
		if (Ctx.bWebbed) return Pick(Rand, Lines(TEXT("webbed")));
		if (Ctx.bImpressed) return Pick(Rand, Lines(TEXT("impressed")));
		if (Ctx.bGreeting) return Pick(Rand, Lines(bNight ? TEXT("greet_night") : TEXT("greet")));
		TArray<TW<const TArray<FString>*>> Pools;
		if (const TArray<FString>* R = ByRole.Find(RoleKey(Ctx.Role)); R && R->Num())
		{
			Pools.Add({R, Ctx.Role == ERole::Patron ? 2.0 : 3.0});
		}
		if (const TArray<FString>* P = ByPlace.Find(Ctx.Place); P && P->Num())
		{
			Pools.Add({P, 2.0});
		}
		// indoors, the weather and the hour matter less
		const bool bOutside = Ctx.Role != ERole::Patron && Ctx.Role != ERole::Customer;
		if (bOutside && Ctx.Weather == EWeather::Rain) Pools.Add({&Lines(TEXT("rain")), 4.0});
		if (bOutside && Ctx.Weather == EWeather::Snow) Pools.Add({&Lines(TEXT("snow")), 4.0});
		if (Ctx.Hour >= 0.0)
		{
			if (Ctx.Hour >= 5.0 && Ctx.Hour < 9.0) Pools.Add({&Lines(TEXT("morning")), 1.5});
			else if (Ctx.Hour >= 17.0 && Ctx.Hour < 21.0) Pools.Add({&Lines(TEXT("evening")), 1.5});
			else if (bNight && bOutside) Pools.Add({&Lines(TEXT("night")), 2.0});
		}
		if (Ctx.Role == ERole::Folk || Ctx.Role == ERole::Patron || Ctx.Role == ERole::Customer || Ctx.Role == ERole::Noble)
		{
			Pools.Add({&Lines(TEXT("any")), 3.0});
		}
		if (!Pools.Num())
		{
			Pools.Add({&Lines(TEXT("any")), 1.0});
		}
		return Pick(Rand, *Weighted(Rand, Pools));
	}

	FString FData::ScatterLine(FRand& Rand) const
	{
		return Pick(Rand, Lines(TEXT("scatter")));
	}

	FString FData::PersonName(FRand& Rand, ERole Role, bool bFemale) const
	{
		// names.ts personName: titles by role
		static const TArray<FString> Empty = {TEXT("Someone")};
		const auto List = [this](const TCHAR* Key) -> const TArray<FString>&
		{
			const TArray<FString>* L = Names.Find(Key);
			return L && L->Num() ? *L : Empty;
		};
		const FString First = Pick(Rand, List(bFemale ? TEXT("female") : TEXT("male")));
		const FString Last = Pick(Rand, List(TEXT("surnames")));
		switch (Role)
		{
		case ERole::Guard: return FString::Printf(TEXT("Guard %s"), *Last);
		case ERole::Knight: return bFemale ? FString::Printf(TEXT("Dame %s"), *First) : FString::Printf(TEXT("Sir %s %s"), *First, *Last);
		case ERole::Lady: return FString::Printf(TEXT("Lady %s %s"), *First, *Last);
		case ERole::Noble: return FString::Printf(TEXT("%s %s %s"), bFemale ? TEXT("Lady") : TEXT("Lord"), *First, *Last);
		case ERole::Fighter: return FString::Printf(TEXT("%s %s"), *First, *Pick(Rand, List(TEXT("fighter_epithets"))));
		case ERole::Mage: return FString::Printf(TEXT("%s %s"), *First, *Pick(Rand, List(TEXT("mage_epithets"))));
		default: return FString::Printf(TEXT("%s %s"), *First, *Last);
		}
	}

	double WeatherScale(EKind Kind, EWeather Weather)
	{
		if (Weather == EWeather::None)
		{
			return 1.0;
		}
		if (Kind == EKind::Inn || Kind == EKind::Shop)
		{
			return 1.2;
		}
		if (Kind == EKind::Town)
		{
			return Weather == EWeather::Rain ? 0.5 : 0.6;
		}
		return 0.8;
	}

	bool TorchHours(double Hour)
	{
		// system.kod's night (before 6, after 20) and the end of dusk
		return Hour >= 0.0 && (Hour >= 19.0 || Hour < 6.0);
	}

	const TCHAR* RoleKey(ERole Role)
	{
		switch (Role)
		{
		case ERole::Patron: return TEXT("patron");
		case ERole::Customer: return TEXT("customer");
		case ERole::Guard: return TEXT("guard");
		case ERole::Knight: return TEXT("knight");
		case ERole::Lady: return TEXT("lady");
		case ERole::Noble: return TEXT("noble");
		case ERole::Fighter: return TEXT("fighter");
		case ERole::Mage: return TEXT("mage");
		case ERole::Spectator: return TEXT("spectator");
		default: return TEXT("folk");
		}
	}

	FString RoleLabel(ERole Role)
	{
		switch (Role)
		{
		case ERole::Patron: return TEXT("Patron");
		case ERole::Customer: return TEXT("Customer");
		case ERole::Guard: return TEXT("Guard");
		case ERole::Knight: return TEXT("Knight");
		case ERole::Lady: return TEXT("Lady of the court");
		case ERole::Noble: return TEXT("Noble");
		case ERole::Fighter: return TEXT("Arena fighter");
		case ERole::Mage: return TEXT("Arena mage");
		case ERole::Spectator: return TEXT("Spectator");
		default: return TEXT("Townsperson");
		}
	}

	// ------------------------------------------------------------------ looks

	int32 TwoColorXlat(int32 Cloth, int32 Skin)
	{
		if (Cloth == DGREEN)
		{
			return 121 + Skin;  // XLAT_REDTODGREEN1..3
		}
		if (Cloth == BLACK)
		{
			return 124 + Skin;  // XLAT_REDTOBLACK1..3
		}
		return 0x87 + 11 * Cloth + Skin;  // XLAT_BASE_VALUE
	}

	FLook RandomLook(FRand& Rand)
	{
		FLook L;
		const bool bFemale = Rand() < 0.5;
		const FPerson P = Person(Rand, bFemale, L);
		TArray<TW<const FTop*>> TopList;
		for (const FTop& T : Tops)
		{
			TopList.Add({&T, T.Weight[P.Sex]});
		}
		const FTop& Top = *Weighted(Rand, TopList);
		int32 Shirt, Sleeves;
		if (Top.Kind == ETop::Royal)
		{
			const int32 C1 = Weighted(Rand, RoyalColours);
			Shirt = TwoColorXlat(C1, Weighted(Rand, Without(RoyalColours, C1)));
			// not the second colour on the arms: it's their hands' (they'd look gloved)
			Sleeves = TwoColorXlat(C1, P.Skin);
		}
		else
		{
			Shirt = TwoColorXlat(Weighted(Rand, Top.Kind == ETop::Robe ? RobeColours : ShirtColours), P.Skin);
			// item.kod SetNakedArms: skin all over
			Sleeves = Top.Kind == ETop::Bare ? TwoColorXlat(P.Skin, P.Skin) : Shirt;
		}
		if (Top.Kind == ETop::Robe)
		{
			L.Legs = RobeLegs[P.Sex];
			L.LegsXlat = Shirt;
		}
		else
		{
			TArray<TW<const FBottom*>> BottomList;
			for (const FBottom& B : Bottoms)
			{
				BottomList.Add({&B, B.Weight[P.Sex]});
			}
			const FBottom& Bottom = *Weighted(Rand, BottomList);
			L.Legs = Bottom.Legs[P.Sex];
			L.LegsXlat = TwoColorXlat(Weighted(Rand, Bottom.bSkirt ? SkirtColours : PantsColours), P.Skin);
		}
		L.Body = Top.Body[P.Sex];
		L.BodyXlat = Shirt;
		L.LeftArm = Top.Arms[P.Sex][0];
		L.RightArm = Top.Arms[P.Sex][1];
		L.ArmsXlat = Sleeves;
		return L;
	}

	FLook GuardLook(FRand& Rand, int32 Livery)
	{
		// a town guard in the town's colours: leather, a plain helm, a sword or mace, sometimes a shield
		FLook L;
		const FPerson P = Person(Rand, Rand() < 0.25, L);
		const int32 Shirt = TwoColorXlat(Livery, P.Skin);
		L.Body = P.Sex ? Armour[TEXT("leather")].Value : Armour[TEXT("leather")].Key;
		L.BodyXlat = Shirt;
		L.LeftArm = ShirtArms[P.Sex][0];
		L.RightArm = ShirtArms[P.Sex][1];
		L.ArmsXlat = Shirt;
		L.Legs = PlainLegs[P.Sex];
		L.LegsXlat = TwoColorXlat(Weighted(Rand, DarkPants), P.Skin);
		L.Hat = Helms[Rand() < 0.75 ? TEXT("simple") : TEXT("war")];
		L.bHatHidesHair = true;
		L.Weapon = Rand() < 0.6 ? TEXT("swordov") : TEXT("maceov");
		if (Rand() < 0.5)
		{
			L.Shield = TEXT("metlshld");
		}
		return L;
	}

	FLook KnightLook(FRand& Rand, const FString& Faction)
	{
		// a knight of an army: plate, gauntlets, the plain helm, the army's shield in its colour, a weapon
		FLook L;
		const FPerson P = Person(Rand, Rand() < 0.35, L);
		const int32 Colour = Faction == TEXT("duke") ? PURPLE : ORANGE;
		const int32 Shirt = TwoColorXlat(Colour, P.Skin);
		L.Body = P.Sex ? Armour[TEXT("plate")].Value : Armour[TEXT("plate")].Key;
		L.BodyXlat = Shirt;
		L.LeftArm = GauntletArms[P.Sex][0];
		L.RightArm = GauntletArms[P.Sex][1];
		L.ArmsXlat = Shirt;
		L.Legs = Pants[P.Sex];
		L.LegsXlat = TwoColorXlat(Weighted(Rand, DarkPants), P.Skin);
		L.Hat = Helms[TEXT("simple")];
		L.bHatHidesHair = true;
		L.Weapon = Weighted(Rand, Strings({{TEXT("swordov"), 4}, {TEXT("maceov"), 2}, {TEXT("hamrov"), 1}, {TEXT("axeov"), 1}, {TEXT("scimov"), 1}}));
		// the army's soldier shield (soldshld/*.kod: gshE.bgf in its colour); its emblem has no sprite of ours yet
		L.Shield = TEXT("gshe");
		L.ShieldXlat = TwoColorXlat(Colour, Colour);
		return L;
	}

	FLook LadyLook(FRand& Rand)
	{
		FLook L;
		const FPerson P = Person(Rand, true, L);
		const int32 Robe = TwoColorXlat(Weighted(Rand, RoyalColours), P.Skin);
		L.Body = TEXT("btd");
		L.BodyXlat = Robe;
		L.LeftArm = TEXT("bld");
		L.RightArm = TEXT("brd");
		L.ArmsXlat = Robe;
		L.Legs = RobeLegs[1];
		L.LegsXlat = Robe;
		L.Hat = Pick(Rand, Circlets);
		return L;
	}

	FLook NobleLook(FRand& Rand)
	{
		FLook L;
		const FPerson P = Person(Rand, Rand() < 0.45, L);
		const int32 C1 = Weighted(Rand, RoyalColours);
		const int32 C2 = Weighted(Rand, Without(RoyalColours, C1));
		if (Rand() < 0.85)
		{
			L.Hat = Pick(Rand, Circlets);
		}
		if (Rand() < 0.5)
		{
			const int32 Robe = TwoColorXlat(C1, P.Skin);
			L.Body = RobeBody[P.Sex];
			L.BodyXlat = Robe;
			L.LeftArm = RobeArms[P.Sex][0];
			L.RightArm = RobeArms[P.Sex][1];
			L.ArmsXlat = Robe;
			L.Legs = RobeLegs[P.Sex];
			L.LegsXlat = Robe;
			return L;
		}
		const bool bSkirt = P.bFemale && Rand() < 0.6;
		L.Body = ShirtBody[P.Sex];
		L.BodyXlat = TwoColorXlat(C1, C2);
		L.LeftArm = ShirtArms[P.Sex][0];
		L.RightArm = ShirtArms[P.Sex][1];
		L.ArmsXlat = TwoColorXlat(C1, P.Skin);
		L.Legs = bSkirt ? RobeLegs[1] : Pants[P.Sex];
		TArray<TW<int32>> LegColours = RoyalColours;
		if (!bSkirt)
		{
			LegColours.Append(DarkPants);
		}
		L.LegsXlat = TwoColorXlat(Weighted(Rand, LegColours), P.Skin);
		return L;
	}

	FLook FighterLook(FRand& Rand, const FString* InArmour, const FString* InWeapon, double ShieldChance, double HelmChance, int32 Female)
	{
		FLook L;
		const FPerson P = Person(Rand, Female >= 0 ? Female != 0 : Rand() < 0.4, L);
		const FString ArmourKey = InArmour ? *InArmour
			: Weighted(Rand, Strings({{TEXT("leather"), 3}, {TEXT("chain"), 3}, {TEXT("scale"), 2}, {TEXT("plate"), 2}, {TEXT("nerudite"), 1}}));
		const TPair<FString, FString>* Body = Armour.Find(ArmourKey);
		const int32 Shirt = TwoColorXlat(Weighted(Rand, ShirtColours), P.Skin);
		const FString Kind = Weighted(Rand, Strings({{TEXT("simple"), 1.5}, {TEXT("war"), 1}, {TEXT("knight"), 1}, {TEXT("ornate"), 0.5}, {TEXT("dark"), 0.5},
			{TEXT("legion"), 0.5}, {TEXT(""), 2}}));
		const FString Helmed = HelmChance < 0.0 ? Kind : Rand() < HelmChance ? (Kind.IsEmpty() ? FString(TEXT("simple")) : Kind) : FString();
		L.Body = Body ? (P.Sex ? Body->Value : Body->Key) : FString(ShirtBody[P.Sex]);
		L.BodyXlat = Shirt;
		const bool bGauntlets = Rand() < 0.5;
		L.LeftArm = (bGauntlets ? GauntletArms : ShirtArms)[P.Sex][0];
		L.RightArm = (bGauntlets ? GauntletArms : ShirtArms)[P.Sex][1];
		L.ArmsXlat = Shirt;
		L.Legs = (Rand() < 0.65 ? Pants : PlainLegs)[P.Sex];
		L.LegsXlat = TwoColorXlat(Weighted(Rand, PantsColours), P.Skin);
		if (!Helmed.IsEmpty())
		{
			L.Hat = Helms[Helmed];
			L.bHatHidesHair = true;
		}
		L.Weapon = InWeapon ? *InWeapon : Pick(Rand, Weapons);
		if (Rand() < ShieldChance)
		{
			L.Shield = Pick(Rand, Shields);
		}
		return L;
	}

	FLook MageLook(FRand& Rand, int32 Female)
	{
		FLook L;
		const FPerson P = Person(Rand, Female >= 0 ? Female != 0 : Rand() < 0.45, L);
		const TArray<TW<int32>> Deep = {{BLUE, 2}, {PURPLE, 2}, {RED, 1}, {BLACK, 1.5}, {DGREEN, 1}, {SKY, 0.5}};
		const int32 Robe = TwoColorXlat(Weighted(Rand, Deep), P.Skin);
		L.Body = RobeBody[P.Sex];
		L.BodyXlat = Robe;
		L.LeftArm = RobeArms[P.Sex][0];
		L.RightArm = RobeArms[P.Sex][1];
		L.ArmsXlat = Robe;
		L.Legs = RobeLegs[P.Sex];
		L.LegsXlat = Robe;
		return L;
	}

	bool IsImpressiveBody(const FString& Bgf)
	{
		const FString F = FPaths::GetBaseFilename(Bgf).ToLower();
		for (const TCHAR* Key : {TEXT("chain"), TEXT("scale"), TEXT("plate"), TEXT("nerudite")})
		{
			const TPair<FString, FString>& A = Armour[Key];
			if (F == A.Key || F == A.Value)
			{
				return true;
			}
		}
		return false;
	}

	void ToNetObject(const FLook& L, bool bTorch, FMRNetObject& Out)
	{
		// townsfolk.ts lookFor, standing: player.kod SendOverlays' order (arms, legs, head, mouth, eyes, nose,
		// the hair unless a helmet took it, then what's worn and held). The poses are our sprite body's actions.
		using namespace MRMsg;
		Out = FMRNetObject();
		Out.Flags = OF_PLAYER;
		Out.Icon = L.Body + TEXT(".bgf");
		Out.Xlat = L.BodyXlat;
		Out.Animation.Type = ANIMATE_NONE;
		Out.Animation.Group = 1;
		const bool bArmed = !L.Weapon.IsEmpty();
		const bool bShield = !bTorch && !L.Shield.IsEmpty();
		TArray<FMRNetOverlay>& V = Out.OverlayParts;
		V.Add(Overlay(L.LeftArm, HsLeftHand, L.ArmsXlat, ANIMATE_NONE, (bTorch || bShield) ? 7 : 1));
		V.Add(Overlay(L.RightArm, HsRightHand, L.ArmsXlat, ANIMATE_NONE, bArmed ? 17 : 1));
		V.Add(Overlay(L.Legs, HsLegs, L.LegsXlat, ANIMATE_NONE, 1));
		V.Add(Overlay(L.Head, HsHead, L.SkinXlat, ANIMATE_NONE, 1));
		V.Add(Overlay(L.Mouth, HsMouth, L.SkinXlat, ANIMATE_NONE, 1));
		V.Add(Overlay(L.Eyes, HsEyes, L.SkinXlat, ANIMATE_NONE, 1));
		V.Add(Overlay(L.Nose, HsNose, L.SkinXlat, ANIMATE_NONE, 1));
		if (!L.Hair.IsEmpty() && !(L.bHatHidesHair && !L.Hat.IsEmpty()))
		{
			V.Add(Overlay(L.Hair, HsToupee, L.HairXlat, ANIMATE_NONE, 1));
		}
		else if (!L.Hat.IsEmpty())
		{
			// bald, or the hair under a helmet: the bald "hair", so the hat that follows on the same hotspot is
			// taken for an item, not for the hair (MRNetLook FirstItemOverlay)
			V.Add(Overlay(TEXT("blank"), HsToupee, 0, ANIMATE_NONE, 1));
		}
		if (!L.Hat.IsEmpty())
		{
			V.Add(Overlay(L.Hat, HsToupee, 0, ANIMATE_NONE, 1));
		}
		if (bArmed)
		{
			V.Add(Overlay(L.Weapon, HsRightWeapon, 0, ANIMATE_NONE, 4));
		}
		if (bShield)
		{
			V.Add(Overlay(L.Shield, HsLeftWeapon, L.ShieldXlat, ANIMATE_NONE, 2));
		}
		if (bTorch)
		{
			V.Add(Overlay(TorchOverlay, HsLeftWeapon, 0, ANIMATE_CYCLE, 1, 60, 6));
		}
	}

	bool ToAppearance(const FLook& L, bool bTorch, FMRSpriteAppearance& Out)
	{
		FMRNetObject O;
		ToNetObject(L, bTorch, O);
		return MRNetLook::AppearanceFromObject(O, Out);
	}

	FName PoseAction(EPose Pose)
	{
		switch (Pose)
		{
		case EPose::Wave: return TEXT("wave");
		case EPose::Point: return TEXT("point");
		case EPose::Dance: return TEXT("dance");
		case EPose::Cheer: return TEXT("dance");  // Shards holds one frame of the dance; ours dances
		case EPose::Cast: return TEXT("cast");
		case EPose::Attack: return TEXT("weapon_attack");
		default: return NAME_None;
		}
	}
}

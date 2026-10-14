#pragma once

#include "CoreMinimal.h"
#include "Character/MRSpriteAppearance.h"

struct FMRNetObject;

/**
 * Ambient life's data and looks (docs/adr/0013-ambient-life.md), from Meridian Shards (ours):
 * - which rooms get a crowd and how busy they are by the hour, what people say and their names:
 *   data/ambient/ambient.json (tools/world/ambient_data.py reads them from Shards' ambientRooms.ts,
 *   chatter.ts and names.ts);
 * - people's looks (townsfolk.ts): dressed as the server dresses a player (player.kod SendOverlays,
 *   util.kod EncodeTwoColorXLAT), as an FMRNetObject that MRNetLook turns into a sprite appearance.
 */
namespace MRAmbient
{
	enum class EKind : uint8 { Town, Inn, Castle, Arena, Shop };
	enum class ERole : uint8 { Folk, Patron, Customer, Guard, Knight, Lady, Noble, Fighter, Mage, Spectator };
	/** The arms' and legs' animations: standing, walking and the emotes (townsfolk.ts Pose) */
	enum class EPose : uint8 { Stand, Walk, Wave, Point, Dance, Cast, Attack, Cheer };
	enum class EWeather : uint8 { None, Rain, Snow };

	/** mulberry32, as Shards' seededRandom: [0, 1) */
	struct FRand
	{
		uint32 State = 0;
		explicit FRand(uint32 Seed = 0) : State(Seed) {}
		double operator()()
		{
			State += 0x6D2B79F5u;
			uint32 T = State;
			T = (T ^ (T >> 15)) * (T | 1u);
			T ^= T + (T ^ (T >> 7)) * (T | 61u);
			return double(T ^ (T >> 14)) / 4294967296.0;
		}
		int32 Index(int32 Num) { return Num > 0 ? FMath::Min(Num - 1, int32((*this)() * Num)) : 0; }
	};

	/** A room's crowd (ambientRooms.ts AmbientRoomSpec) */
	struct FRoomSpec
	{
		EKind Kind = EKind::Town;
		/** How many at the Normal setting (guards besides) */
		int32 Count = 0;
		FString Place;
		int32 Guards = 0;
		/** Towns: the guards' colour (XLAT_TO_*); castles: whose (duke, princess) */
		int32 Livery = 6;
		FString Faction;
		bool bArena = false;
	};

	/** What someone wears and carries (townsfolk.ts Appearance and Gear); bitmaps without ".bgf", lower case */
	struct FLook
	{
		bool bFemale = false;
		FString Body;
		int32 BodyXlat = 0;
		FString LeftArm, RightArm;
		int32 ArmsXlat = 0;
		FString Legs;
		int32 LegsXlat = 0;
		FString Head, Eyes, Nose, Mouth;
		/** Empty when bald */
		FString Hair;
		int32 HairXlat = 0;
		int32 SkinXlat = 1;
		FString Weapon;
		FString Shield;
		int32 ShieldXlat = 0;
		FString Hat;
		bool bHatHidesHair = false;
	};

	/** What a line of speech is about (chatter.ts ChatterContext) */
	struct FChatter
	{
		FString Place;
		ERole Role = ERole::Folk;
		/** The game hour, or -1 when not known */
		double Hour = -1.0;
		EWeather Weather = EWeather::None;
		bool bGreeting = false;
		bool bImpressed = false;
		bool bWon = false;
		bool bLost = false;
		bool bBlinded = false;
		bool bWebbed = false;
		/** An arena fan: their fighter's name, and 0 cheering, 1 won, -1 lost */
		FString FanOf;
		int32 FanOutcome = 0;
	};

	/** data/ambient/ambient.json, loaded once */
	class UNREALMERIDIAN_API FData
	{
	public:
		static const FData& Get();
		/** Reload (tests, MRUIReload) */
		static void Reload();

		bool IsLoaded() const { return bLoaded; }
		/** The crowd of a room by its file ("razainn.roo"), or null */
		const FRoomSpec* Room(const FString& RoomFile) const { return Rooms.Find(RoomFile.ToLower()); }
		/** How busy a kind of room is at a game hour (-1: not known, as by day) */
		double BusyAt(EKind Kind, double Hour) const;
		bool IsErrandRoom(const FString& RoomFile) const;
		FString ChatterLine(FRand& Rand, const FChatter& Ctx) const;
		FString ScatterLine(FRand& Rand) const;
		FString PersonName(FRand& Rand, ERole Role, bool bFemale) const;

	private:
		bool bLoaded = false;
		TMap<FString, FRoomSpec> Rooms;
		TMap<FString, TArray<double>> Busy;
		TArray<FString> ErrandRooms;
		TMap<FString, TArray<FString>> Chatter;
		TMap<FString, TArray<FString>> ByPlace;
		TMap<FString, TArray<FString>> ByRole;
		TMap<FString, TArray<FString>> Names;
		void Load();
		const TArray<FString>& Lines(const TCHAR* Pool) const;
	};

	/** ambientRooms.ts weatherScale: rain and snow thin the streets and fill the inns */
	UNREALMERIDIAN_API double WeatherScale(EKind Kind, EWeather Weather);
	/** ambientRooms.ts torchHours: dark enough for a torch */
	UNREALMERIDIAN_API bool TorchHours(double Hour);
	/** chatter.ts Role as a key in ambient.json ("folk", "patron"...) */
	UNREALMERIDIAN_API const TCHAR* RoleKey(ERole Role);
	/** What the inspect window calls a role ("Townsperson", "Patron"...) */
	UNREALMERIDIAN_API FString RoleLabel(ERole Role);

	// --- looks (townsfolk.ts)
	/** util.kod EncodeTwoColorXLAT: red parts to Cloth, blue parts to Skin (XLAT_TO_* colours) */
	UNREALMERIDIAN_API int32 TwoColorXlat(int32 Cloth, int32 Skin);
	UNREALMERIDIAN_API FLook RandomLook(FRand& Rand);
	UNREALMERIDIAN_API FLook GuardLook(FRand& Rand, int32 Livery);
	UNREALMERIDIAN_API FLook KnightLook(FRand& Rand, const FString& Faction);
	UNREALMERIDIAN_API FLook LadyLook(FRand& Rand);
	UNREALMERIDIAN_API FLook NobleLook(FRand& Rand);
	/** An arena fighter: Armour a key ("leather", "chain", "scale", "plate", "nerudite", "" a shirt) or random when unset */
	UNREALMERIDIAN_API FLook FighterLook(FRand& Rand, const FString* Armour = nullptr, const FString* Weapon = nullptr, double ShieldChance = 0.6, double HelmChance = -1.0, int32 Female = -1);
	UNREALMERIDIAN_API FLook MageLook(FRand& Rand, int32 Female = -1);
	/** Whether a player's body art is armour worth a second look (chain, scale, plate, nerudite) */
	UNREALMERIDIAN_API bool IsImpressiveBody(const FString& Bgf);
	/** The look as the server would send it (player.kod SendOverlays), with a lit torch in the left hand or not */
	UNREALMERIDIAN_API void ToNetObject(const FLook& Look, bool bTorch, FMRNetObject& Out);
	/** The sprite appearance for a look (MRNetLook::AppearanceFromObject on ToNetObject); false if it can't be drawn */
	UNREALMERIDIAN_API bool ToAppearance(const FLook& Look, bool bTorch, FMRSpriteAppearance& Out);
	/** Our sprite body's action for an emote (player_actions.json), or none for standing and walking */
	UNREALMERIDIAN_API FName PoseAction(EPose Pose);
}

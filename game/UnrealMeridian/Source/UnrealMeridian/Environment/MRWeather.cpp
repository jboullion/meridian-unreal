#include "Environment/MRWeather.h"

namespace MRWeather
{
	bool RollStorm(int64 GameDay, int32 Zone, int32 Seed, int32 ChancePercent)
	{
		// Random(1, 100) <= chance, from a hash instead of the server's random generator
		uint64 X = uint64(GameDay) * 0x9E3779B97F4A7C15ull ^ uint64(uint32(Zone)) * 0xC2B2AE3D27D4EB4Full ^ uint64(uint32(Seed)) * 0x165667B19E3779F9ull;
		X ^= X >> 33;
		X *= 0xFF51AFD7ED558CCDull;
		X ^= X >> 33;
		X *= 0xC4CEB9FE1A85EC53ull;
		X ^= X >> 33;
		const int32 Roll = int32(X % 100ull) + 1;
		return Roll <= ChancePercent;
	}

	int64 RunStartDay(int64 GameDay, int32 Zone, int32 Seed, int32 ChancePercent)
	{
		const bool bStorm = RollStorm(GameDay, Zone, Seed, ChancePercent);
		int64 Day = GameDay;
		for (int32 i = 0; i < 60 && RollStorm(Day - 1, Zone, Seed, ChancePercent) == bStorm; ++i)
		{
			--Day;
		}
		return Day;
	}

	int32 MaskByName(const FString& Name)
	{
		// kod blakston.khd WEATHER_MASK_*: one hex digit per season (winter high, spring low), then
		// sound; per digit 1 rain, 2 snow, 4 sand
		static const TMap<FString, int32> Masks = {
			{TEXT("NONE"), 0x0}, {TEXT("SOUND"), 0x1},
			{TEXT("DEFAULT"), 0x21111}, {TEXT("DEFAULT_NS"), 0x21110},
			{TEXT("DESERT"), 0x44441}, {TEXT("DESERT_NS"), 0x44440},
			{TEXT("TUNDRA"), 0x22221}, {TEXT("TUNDRA_NS"), 0x22220},
			{TEXT("JUNGLE"), 0x11111}, {TEXT("JUNGLE_NS"), 0x11110},
			{TEXT("MOUNTAINS"), 0x22111}, {TEXT("MOUNTAINS_NS"), 0x22110},
		};
		const int32* Found = Masks.Find(Name.ToUpper());
		return Found ? *Found : -1;
	}

	EMRWeatherKind KindFor(int32 Mask, int32 Season)
	{
		if (Mask < 0 || Season < 0 || Season > 3)
		{
			return EMRWeatherKind::None;
		}
		const int32 Digit = (Mask >> (4 * (Season + 1))) & 0xF;
		if (Digit & 0x4)
		{
			return EMRWeatherKind::Sand;
		}
		if (Digit & 0x2)
		{
			return EMRWeatherKind::Snow;
		}
		if (Digit & 0x1)
		{
			return EMRWeatherKind::Rain;
		}
		return EMRWeatherKind::None;
	}

	float StormAmount(bool bStorm, double SecondsSince, double RampIn, double RampOut)
	{
		const double S = FMath::Max(0.0, SecondsSince);
		return bStorm ? float(FMath::SmoothStep(0.0, RampIn, S)) : float(1.0 - FMath::SmoothStep(0.0, RampOut, S));
	}

	float Cover(bool bFalling, double SecondsSince, double BuildSeconds, double FadeSeconds)
	{
		const double S = FMath::Max(0.0, SecondsSince);
		return bFalling ? float(FMath::Clamp(S / BuildSeconds, 0.0, 1.0)) : float(FMath::Clamp(1.0 - S / FadeSeconds, 0.0, 1.0));
	}
}

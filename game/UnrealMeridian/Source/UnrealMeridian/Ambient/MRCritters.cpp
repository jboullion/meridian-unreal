#include "Ambient/MRCritters.h"

#include "Ambient/MRAmbientFigure.h"
#include "Audio/MRAudioSubsystem.h"
#include "Character/MRSpriteData.h"
#include "Core/MRUnits.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UnrealMeridian.h"
#include "Net/MRNetWorld.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
#include "World/MRBgf.h"
#include "Zones/MRZoneSubsystem.h"

using namespace MRAmbient;

namespace
{
	constexpr double SQ = MRUnits::CmPerSquare;
	/** Fewer than the habitats say: half (Shards 2026-10-12: there were too many about) */
	constexpr double Density = 0.5;
	/** Spawned this far from the player, and gone past the last */
	constexpr double SpawnNear = 4.0 * SQ, SpawnFar = 12.0 * SQ, Gone = 18.0 * SQ;
	/** They run from the player this close */
	constexpr double ShyCm = 2.0 * SQ;
	/** Floors more than this above or below the player's (rooftops, ledges) get none: 600 fine units */
	constexpr double SameLevelCm = 600.0 / 1024.0 * SQ;
	/** move.c MAX_STEP_HEIGHT: 24 Kod units, 384 fine */
	constexpr double MaxStepCm = 384.0 / 1024.0 * SQ;
	constexpr double CorpseMs = 6000.0, FadeMs = 500.0;
	TAutoConsoleVariable<int32> CVarShowcase(TEXT("mr.Ambient.Showcase"), 0,
		TEXT("Tests and screenshots: critters turn up 1.5-4 squares ahead of the player, in view, rather than out of sight; fish leap often, in view (2: held at the top of the leap)."));
	/** Quieter than a real monster's death: it's only a critter (Shards CRITTER_DEATH_VOLUME) */
	constexpr float DeathVolume = 0.35f;

	/** critters.ts SPECIES: the Kod monster classes' art, animations and sounds (giarat, centip, spdrbaby, redant, scorpion, bunny .kod) */
	const FMRCritters::FSpecies Table[] = {
		{TEXT("rat"), TEXT("GiantRat"), TEXT("rat.bgf"), TEXT("ratx.bgf"), 75, 2, 6, 0.4f, 3.4f, 1.f, 4.f, 500.f, 4000.f, TEXT("rat_dth.ogg"), 1.5f, true},
		{TEXT("centipede"), TEXT("Centipede"), TEXT("centip.bgf"), TEXT("centipx.bgf"), 120, 2, 6, 0.18f, 1.1f, 0.5f, 2.5f, 800.f, 5000.f, TEXT("cnt_dth.ogg"), 1.f, false},
		{TEXT("babySpider"), TEXT("SpiderBaby"), TEXT("spdrby.bgf"), TEXT("spdrbyx.bgf"), 150, 1, 3, 0.21f, 2.4f, 0.5f, 2.5f, 500.f, 3500.f, TEXT("bsp_dth.ogg"), 1.f, false},
		{TEXT("spider"), TEXT("SpiderBaby"), TEXT("spdrby.bgf"), TEXT("spdrbyx.bgf"), 150, 1, 3, 0.26f, 2.6f, 0.5f, 3.f, 500.f, 4000.f, TEXT("bsp_dth.ogg"), 1.f, false},
		// no monster sprite of ours: their own bitmap online, the nearest we have offline
		{TEXT("mutantAnt"), nullptr, TEXT("antr.bgf"), TEXT("antrx.bgf"), 125, 1, 3, 0.08f, 2.2f, 0.5f, 3.f, 400.f, 3000.f, TEXT("ant_dth.ogg"), 1.f, false},
		{TEXT("scorpion"), nullptr, TEXT("scorp.bgf"), TEXT("scorpx.bgf"), 200, 2, 3, 0.11f, 1.5f, 0.5f, 2.f, 1000.f, 6000.f, TEXT("scp_dth.ogg"), 1.f, false},
		{TEXT("bunny"), TEXT("Bunny"), TEXT("bunny2.bgf"), TEXT("bunny2x.bgf"), 180, 2, 3, 0.68f, 3.f, 1.f, 4.f, 1500.f, 8000.f, TEXT("bunny_dth.ogg"), 0.3f, true},
		{TEXT("greyBunny"), nullptr, TEXT("bunnygrey.bgf"), TEXT("bunnygreyx.bgf"), 180, 2, 3, 0.56f, 3.f, 1.f, 4.f, 1500.f, 8000.f, TEXT("bunny_dth.ogg"), 0.3f, true},
	};
	enum ESpecies : int32 { Rat, Centipede, BabySpider, Spider, MutantAnt, Scorpion, Bunny, GreyBunny };
	/** Offline, what stands in for a species without a monster sprite of ours */
	const TCHAR* Stand(int32 S)
	{
		return S == MutantAnt ? TEXT("SpiderBaby") : S == Scorpion ? TEXT("Centipede") : S == GreyBunny ? TEXT("Bunny") : Table[S].Monster;
	}

	/** The vaults (each town's vault keeper's room) and Castle Victoria, a dungeon (its rooms say no terrain) */
	const TCHAR* Vaults[] = {TEXT("barvault.roo"), TEXT("bergvault.roo"), TEXT("jasvault.roo"), TEXT("razavault.roo"), TEXT("kocbank.roo")};
	const TCHAR* Dungeons[] = {TEXT("castle1.roo"), TEXT("castle1b.roo"), TEXT("castle1d.roo"), TEXT("throne1.roo"), TEXT("throne2.roo"), TEXT("greeny.roo")};
}

const FMRCritters::FSpecies& FMRCritters::Species(int32 Index)
{
	return Table[FMath::Clamp(Index, 0, int32(UE_ARRAY_COUNT(Table)) - 1)];
}

bool FMRCritters::HabitatOf(const FString& InFile, const TArray<FString>& T, FHabitat& Out)
{
	// critters.ts habitatOf
	const FString File = InFile.ToLower();
	const auto Has = [&T](std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* N : Names)
		{
			if (T.Contains(N))
			{
				return true;
			}
		}
		return false;
	};
	const auto Set = [&Out](const TCHAR* Name, double Count, std::initializer_list<TPair<int32, double>> Mix, bool bWades = false)
	{
		Out.Name = Name;
		Out.Count = Count;
		Out.Mix = Mix;
		Out.bWades = bWades;
		return true;
	};
	for (const TCHAR* V : Vaults)
	{
		if (File == V) return Set(TEXT("vault"), 4, {{Rat, 1}});
	}
	for (const TCHAR* D : Dungeons)
	{
		if (File == D) return Set(TEXT("dungeon"), 5, {{Rat, 3}, {Spider, 2}});
	}
	const FRoomSpec* Spec = FData::Get().Room(File);
	if (Spec && Spec->Kind == EKind::Arena)
	{
		return false;  // the arena has none (its crowd is enough)
	}
	if (!T.Num())
	{
		if (Spec && Spec->Kind == EKind::Town) return Set(TEXT("street"), 4, {{Rat, 1}});
		static const TCHAR* Towns[] = {TEXT("tos"), TEXT("bar"), TEXT("mar"), TEXT("jas"), TEXT("cor"), TEXT("raza"), TEXT("koc"), TEXT("berg"), TEXT("jixa")};
		for (const TCHAR* P : Towns)
		{
			if (Spec || File.StartsWith(P)) return Set(TEXT("indoors"), 0.6, {{Rat, 1}});
		}
		return false;
	}
	if (Has({TEXT("SEWERS")})) return Set(TEXT("sewer"), 9, {{Rat, 8}, {Centipede, 2}}, true);
	if (Has({TEXT("CAVES"), TEXT("MINES"), TEXT("LAIR")})) return Set(TEXT("cave"), 6, {{Spider, 6}, {Centipede, 1}});
	if (Has({TEXT("CITY")}) && !Has({TEXT("SHOP"), TEXT("RESIDENCE"), TEXT("GUILDHALL"), TEXT("TEMPLE"), TEXT("CASTLE")})) return Set(TEXT("street"), 4, {{Rat, 1}});
	if (Has({TEXT("SHOP"), TEXT("RESIDENCE"), TEXT("GUILDHALL")})) return Set(TEXT("indoors"), 0.6, {{Rat, 1}});
	if (Has({TEXT("DESERT"), TEXT("BADLANDS")})) return Set(TEXT("desert"), 6, {{Scorpion, 4}, {MutantAnt, 2}});
	if (Has({TEXT("MOUNTAIN")})) return Set(TEXT("mountain"), 6, {{MutantAnt, 4}, {Scorpion, 2}});
	if (Has({TEXT("SWAMP")})) return Set(TEXT("swamp"), 6, {{Centipede, 3}, {BabySpider, 3}}, true);
	if (Has({TEXT("FOREST"), TEXT("FIELDS"), TEXT("HILLS")})) return Set(TEXT("wild"), 8, {{BabySpider, 3}, {Centipede, 3}, {Bunny, 1.5}, {GreyBunny, 0.7}});
	if (Has({TEXT("JUNGLE"), TEXT("ROAD"), TEXT("RIVER"), TEXT("LAKE"), TEXT("BEACH")})) return Set(TEXT("wild"), 6, {{BabySpider, 3}, {Centipede, 3}});
	if (Has({TEXT("NECROPOLIS")})) return Set(TEXT("crypt"), 4, {{Spider, 3}, {Rat, 1}});
	return false;
}

FMRCritters::FMRCritters(UWorld* InWorld, const FHabitat& InHabitat, uint32 Seed, uint32 InIdBase)
	: World(InWorld), Habitat(InHabitat), Rand(Seed), NextId(InIdBase)
{
	// fractional counts (an indoor rat now and then) are decided once a visit
	Extra = Rand();
}

FMRCritters::~FMRCritters()
{
	for (FCritter& C : Critters)
	{
		Remove(C);
	}
}

int32 FMRCritters::NumAlive() const
{
	return Critters.FilterByPredicate([](const FCritter& C) { return C.Mode != EMode::Dead && C.FadeDir >= 0; }).Num();
}

bool FMRCritters::Standable(double X, double Y, double Level, double& OutZ) const
{
	// a floor with room above, clear of walls, about the player's level, dry (or shallow water for swimmers)
	UWorld* W = World.Get();
	const UMRZoneSubsystem* Zones = W ? W->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!Zones)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRCritterFloor), false);
	FCollisionResponseParams WorldOnly;
	WorldOnly.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	FHitResult Hit;
	if (!W->LineTraceSingleByChannel(Hit, FVector(X, Y, Level + 150.0), FVector(X, Y, Level - 300.0), ECC_WorldStatic, Params, WorldOnly)
		|| Hit.ImpactNormal.Z < 0.6 || FMath::Abs(Hit.ImpactPoint.Z - Level) > SameLevelCm)
	{
		return false;
	}
	double Z = Hit.ImpactPoint.Z;
	const int32 Depth = Zones->DepthAt(Hit.ImpactPoint);
	if (Depth > (Habitat.bWades ? 1 : 0))
	{
		return false;
	}
	FHitResult Up;
	if (W->LineTraceSingleByChannel(Up, FVector(X, Y, Z + 5.0), FVector(X, Y, Z + 55.0), ECC_WorldStatic, Params, WorldOnly))
	{
		return false;
	}
	// clear of walls and props (Shards: no wall within 96 fine units)
	if (W->OverlapAnyTestByChannel(FVector(X, Y, Z + 25.0), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(18.f), Params, WorldOnly))
	{
		return false;
	}
	if (Depth > 0)
	{
		Z += Zones->DepthSinkAt(Hit.ImpactPoint);  // swimming, at the water's surface
	}
	OutZ = Z;
	return true;
}

bool FMRCritters::ClearRun(const FVector& From, const FVector& To) const
{
	// no wall in the way, and floor all along with no step too high
	UWorld* W = World.Get();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MRCritterRun), false);
	FCollisionResponseParams WorldOnly;
	WorldOnly.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	FHitResult Hit;
	if (!W || W->SweepSingleByChannel(Hit, From + FVector(0.0, 0.0, 25.0), To + FVector(0.0, 0.0, 25.0), FQuat::Identity, ECC_WorldStatic,
		FCollisionShape::MakeSphere(12.f), Params, WorldOnly))
	{
		return false;
	}
	double Last = From.Z;
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(From, To) / 27.5));
	for (int32 K = 1; K <= Steps; ++K)
	{
		const FVector P = FMath::Lerp(From, To, double(K) / Steps);
		double Z;
		if (!Standable(P.X, P.Y, Last, Z) || FMath::Abs(Z - Last) > MaxStepCm)
		{
			return false;
		}
		Last = Z;
	}
	return true;
}

int32 FMRCritters::Pick(bool bNight)
{
	double Total = 0.0;
	for (const TPair<int32, double>& M : Habitat.Mix)
	{
		Total += M.Value * (bNight ? Table[M.Key].Night : 1.0);
	}
	double R = Rand() * Total;
	for (const TPair<int32, double>& M : Habitat.Mix)
	{
		if ((R -= M.Value * (bNight ? Table[M.Key].Night : 1.0)) < 0.0)
		{
			return M.Key;
		}
	}
	return Habitat.Mix.Last().Key;
}

void FMRCritters::Spawn(const FFrame& F, bool bNight, bool bFadeIn)
{
	// somewhere about the player, out of view (more than 60 degrees off ahead) when it fades in, if it can be
	for (int32 K = 0; K < 30; ++K)
	{
		const bool bFar = K >= 24;
		const bool bShowcase = CVarShowcase.GetValueOnGameThread() != 0 && F.FacingYaw >= 0.f;
		double A = Rand() * 2.0 * UE_DOUBLE_PI;
		if (bShowcase)
		{
			A = FMath::DegreesToRadians(F.FacingYaw) + (Rand() - 0.5) * FMath::DegreesToRadians(70.0);
		}
		else if (bFadeIn && F.FacingYaw >= 0.f && K < 16)
		{
			A = FMath::DegreesToRadians(F.FacingYaw) + UE_DOUBLE_PI + (Rand() - 0.5) * FMath::DegreesToRadians(240.0);
		}
		const double D = bShowcase ? (1.5 + Rand() * 2.5) * SQ : bFar ? SpawnFar + Rand() * (Gone - 2.0 * SQ - SpawnFar) : SpawnNear + Rand() * (SpawnFar - SpawnNear);
		const double X = F.Player.X + FMath::Cos(A) * D, Y = F.Player.Y + FMath::Sin(A) * D;
		double Z;
		if (!Standable(X, Y, F.Player.Z, Z))
		{
			continue;
		}
		FCritter& C = Critters.AddDefaulted_GetRef();
		C.Id = NextId++;
		C.Species = Pick(bNight);
		C.Pos = FVector(X, Y, Z);
		C.Yaw = float(Rand() * 360.0);
		C.Timer = Rand() * 3000.0;
		C.Pace = Table[C.Species].Pace * SQ / 1000.0;
		C.Fade = bFadeIn ? 0.0 : 1.0;
		C.FadeDir = bFadeIn ? 1 : 0;
		Show(C, true);
		return;
	}
}

bool FMRCritters::Dash(FCritter& C, const FVector* Away)
{
	// a short straight dash the walls and floors allow, away from the player if Away
	const FSpecies& S = Table[C.Species];
	for (int32 K = 0; K < 8; ++K)
	{
		double A = Rand() * 2.0 * UE_DOUBLE_PI;
		if (Away)
		{
			A = FMath::Atan2(C.Pos.Y - Away->Y, C.Pos.X - Away->X) + (Rand() - 0.5) * (UE_DOUBLE_PI / 2.0);
		}
		const double Len = (S.DashMin + Rand() * (S.DashMax - S.DashMin)) * SQ;
		FVector To(C.Pos.X + FMath::Cos(A) * Len, C.Pos.Y + FMath::Sin(A) * Len, C.Pos.Z);
		double Z;
		if (!Standable(To.X, To.Y, C.Pos.Z, Z))
		{
			continue;
		}
		To.Z = Z;
		if (!ClearRun(C.Pos, To))
		{
			continue;
		}
		C.To = To;
		C.Mode = EMode::Dash;
		C.Yaw = float(FMath::RadiansToDegrees(A));
		return true;
	}
	return false;
}

void FMRCritters::Update(double DtMs, const FFrame& F)
{
	if (!F.bHasPlayer || !World.IsValid())
	{
		return;
	}
	const bool bNight = F.Hour >= 0.0 && (F.Hour >= 20.0 || F.Hour < 6.0);
	const double Raw = Habitat.Count * F.Scale * Density;
	const int32 Target = FMath::FloorToInt(Raw) + (Extra < FMath::Fmod(Raw, 1.0) ? 1 : 0);
	if (Target == 0)
	{
		for (FCritter& C : Critters)
		{
			if (C.Mode != EMode::Dead)
			{
				Remove(C);
			}
		}
		Critters.RemoveAll([](const FCritter& C) { return C.Mode != EMode::Dead; });
	}
	else if (!bStarted)
	{
		// already about when we arrive, anywhere around us
		bStarted = true;
		for (int32 K = 0; K < Target; ++K)
		{
			Spawn(F, bNight, false);
		}
		UE_LOG(LogMeridian, Log, TEXT("MRAmbient: %d of %d critters out (%s)"), NumAlive(), Target, *Habitat.Name);
	}
	else
	{
		const int32 Alive = NumAlive();
		if (Alive < Target && (SpawnMs -= DtMs) <= 0.0)
		{
			SpawnMs = 500.0 + Rand() * 2500.0;
			Spawn(F, bNight, true);
		}
		if (Alive > Target)
		{
			// too many (the setting turned down): the farthest go
			TArray<FCritter*> Far;
			for (FCritter& C : Critters)
			{
				if (C.Mode != EMode::Dead && C.FadeDir >= 0)
				{
					Far.Add(&C);
				}
			}
			Far.Sort([&F](const FCritter& A, const FCritter& B) { return FVector::Dist2D(A.Pos, F.Player) > FVector::Dist2D(B.Pos, F.Player); });
			for (int32 K = 0; K < Alive - Target; ++K)
			{
				Far[K]->FadeDir = -1;
			}
		}
	}
	for (FCritter& C : Critters)
	{
		Step(C, DtMs, F.Player);
	}
	for (int32 I = Critters.Num() - 1; I >= 0; --I)
	{
		FCritter& C = Critters[I];
		if ((C.FadeDir < 0 && C.Fade <= 0.0) || (C.Mode != EMode::Dead && FVector::Dist2D(C.Pos, F.Player) > Gone))
		{
			Remove(C);
			Critters.RemoveAt(I);
		}
	}
}

void FMRCritters::Step(FCritter& C, double DtMs, const FVector& Player)
{
	const FSpecies& S = Table[C.Species];
	FVector Velocity = FVector::ZeroVector;
	if (C.FadeDir)
	{
		C.Fade = FMath::Clamp(C.Fade + C.FadeDir * DtMs / FadeMs, 0.0, 1.0);
		if (C.Fade >= 1.0 && C.FadeDir > 0)
		{
			C.FadeDir = 0;
		}
	}
	if (C.FadeDir >= 0 && C.Mode == EMode::Dead)
	{
		if ((C.Timer -= DtMs) <= 0.0 && !C.FadeDir)
		{
			C.FadeDir = -1;
		}
	}
	else if (C.FadeDir >= 0)
	{
		// the player close: the shy ones run
		if (S.bShy && C.Mode == EMode::Rest && FVector::Dist2D(C.Pos, Player) < ShyCm && Dash(C, &Player))
		{
			C.Pace = S.Pace * 1.4 * SQ / 1000.0;
		}
		if (C.Mode == EMode::Rest)
		{
			if ((C.Timer -= DtMs) <= 0.0 && !Dash(C, nullptr))
			{
				C.Timer = 500.0 + Rand() * 1500.0;
			}
		}
		else
		{
			const double D = FVector::Dist2D(C.To, C.Pos);
			const double Move = C.Pace * DtMs;
			if (D <= Move)
			{
				C.Pos = C.To;
				C.Mode = EMode::Rest;
				C.Pace = S.Pace * SQ / 1000.0;
				C.Timer = S.RestMin + Rand() * (S.RestMax - S.RestMin);
			}
			else
			{
				const FVector Dir = (C.To - C.Pos) / D;
				C.Pos += FVector(Dir.X, Dir.Y, 0.0) * Move;
				// the floor along the dash: from where it was to where it's going
				C.Pos.Z += (C.To.Z - C.Pos.Z) * FMath::Min(1.0, Move / D);
				Velocity = FVector(Dir.X, Dir.Y, 0.0) * C.Pace * 1000.0;
			}
		}
	}
	if (AMRAmbientFigure* Fig = C.Figure.Get())
	{
		Fig->Place(C.Pos, C.Yaw, Velocity);
		Fig->SetFade(float(C.Fade));
	}
}

void FMRCritters::Show(FCritter& C, bool bNew)
{
	UWorld* W = World.Get();
	if (!W)
	{
		return;
	}
	AMRAmbientFigure* Fig = C.Figure.Get();
	if (!Fig)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Fig = W->SpawnActor<AMRAmbientFigure>(AMRAmbientFigure::StaticClass(), FTransform::Identity, Params);
		if (!Fig)
		{
			return;
		}
		Fig->AmbientId = C.Id;
		C.Figure = Fig;
	}
	const FSpecies& S = Table[C.Species];
	const bool bDead = C.Mode == EMode::Dead;
	// our monster sprite when we have one; else online the critter's own bitmap, offline the nearest we have
	UMRNetWorldSubsystem* NetWorld = W->GetSubsystem<UMRNetWorldSubsystem>();
	const bool bOnline = NetWorld && NetWorld->IsActive();
	const TCHAR* Monster = S.Monster ? S.Monster : (bOnline ? nullptr : Stand(C.Species));
	const FMRMonsterDef* Def = Monster ? FMRSpriteLibrary::Get().Monsters.Find(Monster) : nullptr;
	if (Def)
	{
		Fig->SetMonster(bDead && !Def->DeadLook.IsNone() ? Def->DeadLook : Def->Look, S.Scale);
	}
	else if (bOnline)
	{
		C.bBitmap = true;
		FMRNetAnimation Stand, Walk;
		Stand.Type = MRMsg::ANIMATE_NONE;
		Stand.Group = 1;
		Walk.Type = MRMsg::ANIMATE_CYCLE;
		Walk.Period = S.WalkPeriod;
		Walk.GroupLow = S.WalkLow;
		Walk.GroupHigh = S.WalkHigh;
		const float Scale = S.Scale;
		const TWeakObjectPtr<AMRAmbientFigure> Weak(Fig);
		NetWorld->FetchBgf(bDead ? S.Dead : S.Icon, [Weak, Stand, Walk, Scale, bDead](TSharedPtr<const FMRBgf> Bgf)
		{
			if (AMRAmbientFigure* F = Weak.Get(); F && Bgf.IsValid())
			{
				F->SetBitmap(Bgf, Stand, bDead ? Stand : Walk, Scale);
			}
		});
	}
	Fig->Place(C.Pos, C.Yaw, FVector::ZeroVector);
	Fig->SetFade(float(C.Fade));
}

void FMRCritters::Kill(FCritter& C)
{
	C.Mode = EMode::Dead;
	C.Timer = CorpseMs;
	C.Fade = 1.0;
	C.FadeDir = 0;
	Show(C, false);
	if (UWorld* W = World.Get())
	{
		UMRAudioSubsystem::Play(W, Table[C.Species].Sound, C.Pos, false, DeathVolume);
	}
}

void FMRCritters::Remove(FCritter& C)
{
	if (AMRAmbientFigure* Fig = C.Figure.Get())
	{
		Fig->Destroy();
	}
	C.Figure.Reset();
}

bool FMRCritters::SquishAhead(const FVector& At, float Yaw, double ReachCm)
{
	FCritter* Best = nullptr;
	double BestD = ReachCm;
	for (FCritter& C : Critters)
	{
		const double D = FVector::Dist2D(C.Pos, At);
		if (C.Mode == EMode::Dead || C.FadeDir || D > BestD)
		{
			continue;
		}
		const float Toward = FMath::RadiansToDegrees(FMath::Atan2(C.Pos.Y - At.Y, C.Pos.X - At.X));
		if (FMath::Abs(FRotator::NormalizeAxis(Toward - Yaw)) < 62.f)
		{
			Best = &C;
			BestD = D;
		}
	}
	if (Best)
	{
		Kill(*Best);
	}
	return Best != nullptr;
}

bool FMRCritters::SquishOnRay(const FVector& Start, const FVector& Dir, double MaxCm, double ReachCm)
{
	FCritter* Best = nullptr;
	double BestOff = ReachCm;
	for (FCritter& C : Critters)
	{
		if (C.Mode == EMode::Dead || C.FadeDir)
		{
			continue;
		}
		const FVector Mid = C.Pos + FVector(0.0, 0.0, 10.0);
		const double Along = FVector::DotProduct(Mid - Start, Dir);
		const double Off = FVector::Dist(Start + Dir * Along, Mid);
		if (Along > 0.0 && Along < MaxCm && Off < BestOff)
		{
			Best = &C;
			BestOff = Off;
		}
	}
	if (Best)
	{
		Kill(*Best);
	}
	return Best != nullptr;
}

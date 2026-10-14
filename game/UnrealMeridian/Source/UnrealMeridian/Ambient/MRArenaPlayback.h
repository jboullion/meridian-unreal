#pragma once

#include "CoreMinimal.h"
#include "Ambient/MRAmbientData.h"

class AMRAmbientFigure;
class FJsonObject;
class UWorld;

/**
 * The Arena of Kraanan as the relay runs it (docs/adr/0013-ambient-life.md): Meridian Shards' ArenaView
 * (packages/world/src/arena/arenaView.ts and arenaWire.ts, ours), playing the relay's messages back about
 * 220 ms behind its clock, so a late or bunched tick doesn't show: each tick is applied when its time comes
 * and the figures glide between ticks.
 *
 * - The fighters and the stands are AMRAmbientFigures in the looks the relay sends (townsfolk.ts Appearance);
 *   their poses are our sprite body's actions; the winner cheers with the weapon put away.
 * - The walls' elements (fire, fog, webs, brambles by their health) and the spells in flight are the
 *   game's own bitmaps, through the server's asset cache.
 * - Events play the game's sounds (swings, hits, cries, spells, frying, coughs, cheers); lines are said over heads.
 * - Not drawn yet: the web on the legs and the blinding ring about the head (no sprites of ours for them).
 */
class UNREALMERIDIAN_API FMRArenaPlayback
{
public:
	FMRArenaPlayback(UWorld* InWorld, int32 InRid);
	~FMRArenaPlayback();

	/** A message from the relay (arenaState: everything; arenaTick: what changed) */
	void Receive(const TSharedPtr<FJsonObject>& M);
	/** One frame at a steady local clock (ms) */
	void Update(double NowMs, double DtMs);
	bool IsReady() const { return bReady; }
	int32 NumFigures() const { return Figures.Num(); }
	const FString& GetPhase() const { return Phase; }

	struct FSpeech
	{
		FVector Anchor;
		FString Text;
	};
	TArray<FSpeech> GetSpeech() const;
	/** The figure nearest a ray (Examine), or 0 */
	uint32 FigureNear(const FVector& Start, const FVector& Dir) const;
	/** Examine on a fighter or spectator: their name, the inspect text and their look */
	bool Inspect(uint32 Id, FString& OutName, FString& OutText, FMRSpriteAppearance& OutLook) const;

private:
	struct FFigure
	{
		uint32 Id = 0;
		int32 Rid = -1;
		FString Name, Arch, Role;
		MRAmbient::FLook Look;
		FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;
		double At = 0.0;
		int32 Angle = 0;
		FString Pose = TEXT("stand");
		int32 Ps = 0, Hp = 0, Mhp = 0, St = 0, Fade = 100, Fd = 0, Fav = -1;
		FString Built;
		FString Say;
		double SayMs = 0.0;
		FVector Feet = FVector::ZeroVector;
		bool bHasFeet = false;
		TWeakObjectPtr<AMRAmbientFigure> Actor;
	};
	struct FField
	{
		int32 Id = 0;
		FString Kind;
		FVector2D Pos;
		double End = 0.0;
		int32 G = 0;
		bool bFading = false;
		TWeakObjectPtr<AMRAmbientFigure> Actor;
	};
	struct FShot
	{
		int32 Id = 0;
		FString Spell;
		FVector2D From, To;
		double T = 0.0, FlightMs = 1000.0;
		TWeakObjectPtr<AMRAmbientFigure> Actor;
	};
	struct FRoster
	{
		FString Name, Arch, Weapon, Spell;
		int32 Wins = 0, Losses = 0, Kills = 0;
		TArray<int32> Champ;
	};

	TWeakObjectPtr<UWorld> World;
	int32 Rid = 0;
	bool bReady = false;
	double Offset = 0.0;
	double Shown = 0.0;
	bool bSync = false;
	double SyncTo = 0.0;
	FString Phase = TEXT("rest");
	int32 Season = 0;
	TMap<FString, FString> ArchLabels, WeaponLabels, SpellLabels;
	TMap<int32, FRoster> Roster;
	TArray<TSharedPtr<FJsonObject>> Queue;
	TMap<uint32, FFigure> Figures;
	TMap<int32, FField> Fields;
	TMap<int32, FShot> Shots;
	double CheerMs = 0.0;

	void Clear();
	void Reset(const TSharedPtr<FJsonObject>& S);
	void Apply(const TSharedPtr<FJsonObject>& T);
	void AddFigure(const TSharedPtr<FJsonObject>& P, double Now);
	void Dress(FFigure& F, bool bForce = false);
	void AddField(const TSharedPtr<FJsonObject>& W);
	void ShowField(FField& F);
	void AddShot(const TSharedPtr<FJsonObject>& W);
	void ReadRoster(const TSharedPtr<FJsonObject>& R);
	void Event(const TSharedPtr<FJsonObject>& E);
	FVector World3(const FVector2D& Roo, FVector* Last = nullptr) const;
	AMRAmbientFigure* SpawnFigure(uint32 AmbientId) const;
	void Sound(const FString& File, const FVector2D& At) const;
	static MRAmbient::FLook ReadLook(const TSharedPtr<FJsonObject>& L);
};

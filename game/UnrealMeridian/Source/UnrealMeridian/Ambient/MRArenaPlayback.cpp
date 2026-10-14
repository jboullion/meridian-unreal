#include "Ambient/MRArenaPlayback.h"

#include "Ambient/MRAmbientFigure.h"
#include "Audio/MRAudioSubsystem.h"
#include "Core/MRUnits.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Net/MRNetWorld.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
#include "World/MRBgf.h"
#include "Zones/MRZoneSubsystem.h"

using namespace MRAmbient;

namespace
{
	/** Played back this far behind the newest tick, and a tick's span (arenaView.ts) */
	constexpr double PlaybackDelayMs = 220.0, TickSpanMs = 100.0;
	constexpr double FieldFadeMs = 700.0;
	/** arenaWire.ts ARENA_STATUS */
	constexpr int32 StWebbed = 1, StBlind = 2, StDazzled = 4, StPoisoned = 8, StWinner = 16, StBeaten = 32;
	/** Ambient ids of the arena's figures, apart from the crowds' (Shards: ambientId) */
	constexpr uint32 ArenaIdBase = 0x60000000u;
	const TCHAR* Moods[] = {TEXT("Content"), TEXT("Cheerful"), TEXT("Thoughtful"), TEXT("Excited"), TEXT("Restless"), TEXT("Bored"), TEXT("Curious"),
		TEXT("Grumpy"), TEXT("Hopeful"), TEXT("Rowdy")};

	FMRNetAnimation Anim(uint8 Type, uint16 Group, uint32 Period = 0, uint16 Low = 0, uint16 High = 0, uint16 Final = 0)
	{
		FMRNetAnimation A;
		A.Type = Type;
		A.Group = Group;
		A.Period = Period;
		A.GroupLow = Low;
		A.GroupHigh = High;
		A.GroupFinal = Final;
		return A;
	}

	/** arenaData.ts ARENA_FIELDS: the elements' art and animation (wallelem/*.kod, monster/bramble.kod) */
	bool FieldArt(const FString& Kind, FString& File, FMRNetAnimation& A)
	{
		using namespace MRMsg;
		if (Kind == TEXT("fire")) { File = TEXT("woflame.bgf"); A = Anim(ANIMATE_CYCLE, 1, 100, 1, 5); return true; }
		if (Kind == TEXT("fog")) { File = TEXT("poisoncl.bgf"); A = Anim(ANIMATE_CYCLE, 1, 240, 1, 5); return true; }
		if (Kind == TEXT("web")) { File = TEXT("webspell.bgf"); A = Anim(ANIMATE_NONE, 1); return true; }
		if (Kind == TEXT("bramble")) { File = TEXT("wothorns.bgf"); A = Anim(ANIMATE_ONCE, 1, 250, 1, 5, 5); return true; }
		return false;
	}

	/** arenaData.ts ARENA_SPELLS: a spell's projectile (art, animation, squares a second) and its cast sound */
	struct FSpellArt
	{
		const TCHAR* Key;
		const TCHAR* File;
		uint32 Period;
		uint16 Low, High;
		double Speed;
		const TCHAR* Sound;
	};
	const FSpellArt Spells[] = {
		{TEXT("fireball"), TEXT("fireball.bgf"), 40, 1, 2, 10.0, TEXT("ffirebal.ogg")},
		{TEXT("lightning"), TEXT("lightnin.bgf"), 40, 2, 3, 12.0, TEXT("fbolt.ogg")},
		{TEXT("blind"), TEXT("blindring.bgf"), 80, 1, 6, 10.0, TEXT("qblind.ogg")},
		{TEXT("dazzle"), TEXT("dazzlestar.bgf"), 80, 1, 6, 10.0, TEXT("sdazzle.ogg")},
		{TEXT("firewall"), nullptr, 0, 0, 0, 0.0, TEXT("farenwala822m.ogg")},
		{TEXT("bramble"), nullptr, 0, 0, 0, 0.0, TEXT("faren.ogg")},
		{TEXT("poisonfog"), nullptr, 0, 0, 0, 0.0, TEXT("qor.ogg")},
		{TEXT("web"), nullptr, 0, 0, 0, 0.0, TEXT("farenwala822m.ogg")},
	};
	const FSpellArt* SpellArt(const FString& Key)
	{
		for (const FSpellArt& S : Spells)
		{
			if (Key == S.Key)
			{
				return &S;
			}
		}
		return nullptr;
	}

	EPose PoseOf(const FString& P)
	{
		return P == TEXT("walk") ? EPose::Walk : P == TEXT("wave") ? EPose::Wave : P == TEXT("point") ? EPose::Point : P == TEXT("dance") ? EPose::Dance
			: P == TEXT("cast") ? EPose::Cast : P == TEXT("attack") ? EPose::Attack : P == TEXT("cheer") ? EPose::Cheer : EPose::Stand;
	}

	/** A bitmap name as our sprites name it ("helmb.bgf" -> "helmb"); Shards' art our sprite build has under another name */
	FString Art(const FString& File)
	{
		FString F = FPaths::GetBaseFilename(File).ToLower();
		static const TMap<FString, FString> Ours = {
			// helmets: one art for both (equipment.json); Shards draws the female's
			{TEXT("helmb"), TEXT("helm")}, {TEXT("warhelmb"), TEXT("warhelm")}, {TEXT("khelmb"), TEXT("khelm")},
			{TEXT("ornhelmb"), TEXT("ornhelma")}, {TEXT("dhelmb"), TEXT("dhelma")}, {TEXT("legionhelmb"), TEXT("legionhelm")},
			// circlets: their worn art as equipment.json names it
			{TEXT("circlet"), TEXT("invcirc")}, {TEXT("ivycirov"), TEXT("ivycircl")}, {TEXT("thocirclov"), TEXT("thocircl")}};
		if (const FString* O = Ours.Find(F))
		{
			return *O;
		}
		return F;
	}

	uint32 NameHash(const FString& S)
	{
		uint32 H = 0x811c9dc5u;
		for (const TCHAR C : S)
		{
			H = (H ^ uint32(C)) * 0x01000193u;
		}
		return H;
	}

	double Num(const TSharedPtr<FJsonValue>& V) { return V.IsValid() && V->Type == EJson::Number ? V->AsNumber() : 0.0; }
}

FMRArenaPlayback::FMRArenaPlayback(UWorld* InWorld, int32 InRid)
	: World(InWorld), Rid(InRid)
{
}

FMRArenaPlayback::~FMRArenaPlayback()
{
	Clear();
}

void FMRArenaPlayback::Clear()
{
	for (TPair<uint32, FFigure>& F : Figures)
	{
		if (AMRAmbientFigure* A = F.Value.Actor.Get()) A->Destroy();
	}
	for (TPair<int32, FField>& F : Fields)
	{
		if (AMRAmbientFigure* A = F.Value.Actor.Get()) A->Destroy();
	}
	for (TPair<int32, FShot>& S : Shots)
	{
		if (AMRAmbientFigure* A = S.Value.Actor.Get()) A->Destroy();
	}
	Figures.Reset();
	Fields.Reset();
	Shots.Reset();
	Queue.Reset();
	bReady = false;
}


MRAmbient::FLook FMRArenaPlayback::ReadLook(const TSharedPtr<FJsonObject>& L)
{
	// townsfolk.ts Appearance as the wire sends it
	FLook Out;
	if (!L.IsValid())
	{
		return Out;
	}
	Out.bFemale = L->GetBoolField(TEXT("female"));
	Out.Body = Art(L->GetStringField(TEXT("body")));
	Out.BodyXlat = int32(L->GetNumberField(TEXT("bodyXlat")));
	const TArray<TSharedPtr<FJsonValue>>* Arms = nullptr;
	if (L->TryGetArrayField(TEXT("arms"), Arms) && Arms->Num() == 2)
	{
		Out.LeftArm = Art((*Arms)[0]->AsString());
		Out.RightArm = Art((*Arms)[1]->AsString());
	}
	Out.ArmsXlat = int32(L->GetNumberField(TEXT("armsXlat")));
	Out.Legs = Art(L->GetStringField(TEXT("legs")));
	Out.LegsXlat = int32(L->GetNumberField(TEXT("legsXlat")));
	Out.Head = Art(L->GetStringField(TEXT("head")));
	Out.Eyes = Art(L->GetStringField(TEXT("eyes")));
	Out.Nose = Art(L->GetStringField(TEXT("nose")));
	Out.Mouth = Art(L->GetStringField(TEXT("mouth")));
	const FString Hair = L->GetStringField(TEXT("hair"));
	Out.Hair = Hair.IsEmpty() ? FString() : Art(Hair);
	Out.HairXlat = int32(L->GetNumberField(TEXT("hairXlat")));
	Out.SkinXlat = int32(L->GetNumberField(TEXT("skinXlat")));
	const TSharedPtr<FJsonObject>* Gear = nullptr;
	if (L->TryGetObjectField(TEXT("gear"), Gear))
	{
		FString W;
		if ((*Gear)->TryGetStringField(TEXT("weapon"), W))
		{
			Out.Weapon = Art(W);
		}
		const TSharedPtr<FJsonObject>* Shield = nullptr;
		if ((*Gear)->TryGetObjectField(TEXT("shield"), Shield))
		{
			Out.Shield = Art((*Shield)->GetStringField(TEXT("file")));
			Out.ShieldXlat = int32((*Shield)->GetNumberField(TEXT("xlat")));
		}
		const TSharedPtr<FJsonObject>* Hat = nullptr;
		if ((*Gear)->TryGetObjectField(TEXT("hat"), Hat))
		{
			Out.Hat = Art((*Hat)->GetStringField(TEXT("file")));
			Out.bHatHidesHair = (*Hat)->GetBoolField(TEXT("hidesHair"));
		}
	}
	return Out;
}

void FMRArenaPlayback::Receive(const TSharedPtr<FJsonObject>& M)
{
	if (!M.IsValid())
	{
		Clear();  // arenaOff, or the relay's gone
		return;
	}
	if (M->GetStringField(TEXT("t")) == TEXT("arenaState"))
	{
		Reset(M);
	}
	else if (bReady)
	{
		Queue.Add(M);
	}
}

void FMRArenaPlayback::ReadRoster(const TSharedPtr<FJsonObject>& R)
{
	FRoster& E = Roster.FindOrAdd(int32(R->GetNumberField(TEXT("rid"))));
	E.Name = R->GetStringField(TEXT("name"));
	E.Arch = R->GetStringField(TEXT("arch"));
	R->TryGetStringField(TEXT("weapon"), E.Weapon);
	R->TryGetStringField(TEXT("spell"), E.Spell);
	const TSharedPtr<FJsonObject>* Rec = nullptr;
	if (R->TryGetObjectField(TEXT("rec"), Rec))
	{
		E.Wins = int32((*Rec)->GetNumberField(TEXT("wins")));
		E.Losses = int32((*Rec)->GetNumberField(TEXT("losses")));
		E.Kills = int32((*Rec)->GetNumberField(TEXT("kills")));
	}
	E.Champ.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Champ = nullptr;
	if (R->TryGetArrayField(TEXT("champ"), Champ))
	{
		for (const TSharedPtr<FJsonValue>& V : *Champ)
		{
			E.Champ.Add(int32(V->AsNumber()));
		}
	}
}

void FMRArenaPlayback::Reset(const TSharedPtr<FJsonObject>& S)
{
	Clear();
	bReady = true;
	const double Now = S->GetNumberField(TEXT("now"));
	Season = int32(S->GetNumberField(TEXT("season")));
	Phase = S->GetStringField(TEXT("phase"));
	const TSharedPtr<FJsonObject>* Labels = nullptr;
	if (S->TryGetObjectField(TEXT("labels"), Labels))
	{
		for (const TPair<const TCHAR*, TMap<FString, FString>*> L : {TPair<const TCHAR*, TMap<FString, FString>*>(TEXT("arch"), &ArchLabels),
			TPair<const TCHAR*, TMap<FString, FString>*>(TEXT("weapon"), &WeaponLabels), TPair<const TCHAR*, TMap<FString, FString>*>(TEXT("spell"), &SpellLabels)})
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if ((*Labels)->TryGetObjectField(L.Key, Obj))
			{
				for (const auto& Pair : (*Obj)->Values)
				{
					L.Value->Add(FString(Pair.Key), Pair.Value->AsString());
				}
			}
		}
	}
	Roster.Reset();
	for (const TSharedPtr<FJsonValue>& R : S->GetArrayField(TEXT("roster")))
	{
		ReadRoster(R->AsObject());
	}
	Shown = Now;
	SyncTo = Now;
	bSync = true;
	for (const TSharedPtr<FJsonValue>& P : S->GetArrayField(TEXT("people")))
	{
		AddFigure(P->AsObject(), Now);
	}
	for (const TSharedPtr<FJsonValue>& F : S->GetArrayField(TEXT("fields")))
	{
		AddField(F->AsObject());
	}
	for (const TSharedPtr<FJsonValue>& Sh : S->GetArrayField(TEXT("shots")))
	{
		AddShot(Sh->AsObject());
	}
}

AMRAmbientFigure* FMRArenaPlayback::SpawnFigure(uint32 AmbientId) const
{
	UWorld* W = World.Get();
	if (!W)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AMRAmbientFigure* A = W->SpawnActor<AMRAmbientFigure>(AMRAmbientFigure::StaticClass(), FTransform::Identity, Params);
	if (A)
	{
		A->AmbientId = AmbientId;
	}
	return A;
}

FVector FMRArenaPlayback::World3(const FVector2D& Roo, FVector* Last) const
{
	const UWorld* W = World.Get();
	const UMRZoneSubsystem* Zones = W ? W->GetSubsystem<UMRZoneSubsystem>() : nullptr;
	if (!Zones)
	{
		return FVector::ZeroVector;
	}
	if (Last)
	{
		// near where it last stood: a short look down from knee height
		const FVector Flat = Zones->RooToWorld(Rid, Roo, false);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MRArenaFloor), false);
		FCollisionResponseParams WorldOnly;
		WorldOnly.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
		if (W->LineTraceSingleByChannel(Hit, FVector(Flat.X, Flat.Y, Last->Z + 60.0), FVector(Flat.X, Flat.Y, Last->Z - 150.0), ECC_WorldStatic, Params, WorldOnly))
		{
			return Hit.ImpactPoint;
		}
	}
	return Zones->RooToWorld(Rid, Roo, true);
}

void FMRArenaPlayback::AddFigure(const TSharedPtr<FJsonObject>& P, double Now)
{
	const uint32 Id = uint32(P->GetNumberField(TEXT("id")));
	FFigure& F = Figures.FindOrAdd(Id);
	F.Id = Id;
	F.Rid = int32(P->GetNumberField(TEXT("rid")));
	F.Name = P->GetStringField(TEXT("name"));
	F.Arch = P->GetStringField(TEXT("arch"));
	F.Role = P->GetStringField(TEXT("role"));
	const TSharedPtr<FJsonObject>* Look = nullptr;
	F.Look = ReadLook(P->TryGetObjectField(TEXT("look"), Look) ? *Look : nullptr);
	F.From = F.To = FVector2D(P->GetNumberField(TEXT("x")), P->GetNumberField(TEXT("y")));
	F.At = Now;
	F.Angle = int32(P->GetNumberField(TEXT("a")));
	F.Pose = P->GetStringField(TEXT("pose"));
	F.Ps = int32(P->GetNumberField(TEXT("ps")));
	F.Hp = int32(P->GetNumberField(TEXT("hp")));
	F.Mhp = int32(P->GetNumberField(TEXT("mhp")));
	F.St = int32(P->GetNumberField(TEXT("st")));
	F.Fade = int32(P->GetNumberField(TEXT("fade")));
	F.Fd = int32(P->GetNumberField(TEXT("fd")));
	double Fav = -1.0;
	P->TryGetNumberField(TEXT("fav"), Fav);
	F.Fav = int32(Fav);
	F.Built.Reset();
	if (!F.Actor.IsValid())
	{
		F.Actor = SpawnFigure(ArenaIdBase + Id);
	}
	Dress(F, true);
}

void FMRArenaPlayback::Dress(FFigure& F, bool bForce)
{
	// the figure's looks for its pose and state (arenaView.ts dress): the winner's weapon put away
	AMRAmbientFigure* A = F.Actor.Get();
	if (!A)
	{
		return;
	}
	A->SetFade(F.Fade / 100.f);
	const bool bWinner = (F.St & StWinner) != 0;
	const FString LookKey = bWinner ? TEXT("w") : TEXT("a");
	if (bForce || !F.Built.StartsWith(LookKey))
	{
		FLook L = F.Look;
		if (bWinner)
		{
			L.Weapon.Reset();
		}
		FMRSpriteAppearance Ap;
		if (ToAppearance(L, false, Ap))
		{
			A->SetPerson(Ap);
		}
	}
	const FString Key = FString::Printf(TEXT("%s|%s|%d"), *LookKey, *F.Pose, F.Ps);
	if (Key != F.Built)
	{
		F.Built = Key;
		A->PlayAction(PoseAction(PoseOf(F.Pose)));
	}
}

void FMRArenaPlayback::AddField(const TSharedPtr<FJsonObject>& W)
{
	FField F;
	F.Id = int32(W->GetNumberField(TEXT("id")));
	F.Kind = W->GetStringField(TEXT("kind"));
	F.Pos = FVector2D(W->GetNumberField(TEXT("x")), W->GetNumberField(TEXT("y")));
	F.End = W->GetNumberField(TEXT("end"));
	F.G = int32(W->GetNumberField(TEXT("g")));
	F.Actor = SpawnFigure(ArenaIdBase + 0x01000000u + uint32(F.Id));
	FField& Kept = Fields.Add(F.Id, F);
	ShowField(Kept);
}

void FMRArenaPlayback::ShowField(FField& F)
{
	FString File;
	FMRNetAnimation A;
	UWorld* W = World.Get();
	UMRNetWorldSubsystem* Net = W ? W->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	AMRAmbientFigure* Actor = F.Actor.Get();
	if (!Actor || !Net || !FieldArt(F.Kind, File, A))
	{
		return;
	}
	// a bramble's look by its health, once grown (bramble.kod SendAnimation: groups 1-5)
	if (F.Kind == TEXT("bramble") && F.G > 0 && F.G < 5)
	{
		A = Anim(MRMsg::ANIMATE_NONE, uint16(FMath::Clamp(F.G, 1, 5)));
	}
	Actor->Place(World3(F.Pos), 0.f, FVector::ZeroVector);
	const TWeakObjectPtr<AMRAmbientFigure> Weak(Actor);
	Net->FetchBgf(File, [Weak, A](TSharedPtr<const FMRBgf> Bgf)
	{
		if (AMRAmbientFigure* Fig = Weak.Get(); Fig && Bgf.IsValid())
		{
			Fig->SetBitmap(Bgf, A, A, 1.f);
		}
	});
}

void FMRArenaPlayback::AddShot(const TSharedPtr<FJsonObject>& W)
{
	const FString Spell = W->GetStringField(TEXT("spell"));
	const FSpellArt* Art = SpellArt(Spell);
	UWorld* Wd = World.Get();
	UMRNetWorldSubsystem* Net = Wd ? Wd->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	if (!Art || !Art->File || !Net)
	{
		return;
	}
	FShot S;
	S.Id = int32(W->GetNumberField(TEXT("id")));
	S.Spell = Spell;
	S.From = FVector2D(W->GetNumberField(TEXT("sx")), W->GetNumberField(TEXT("sy")));
	S.To = FVector2D(W->GetNumberField(TEXT("dx")), W->GetNumberField(TEXT("dy")));
	S.T = W->GetNumberField(TEXT("t"));
	S.FlightMs = FVector2D::Distance(S.From, S.To) / 1024.0 / Art->Speed * 1000.0;
	S.Actor = SpawnFigure(ArenaIdBase + 0x02000000u + uint32(S.Id));
	const FMRNetAnimation A = Anim(MRMsg::ANIMATE_CYCLE, Art->Low, Art->Period, Art->Low, Art->High);
	const TWeakObjectPtr<AMRAmbientFigure> Weak(S.Actor);
	Net->FetchBgf(Art->File, [Weak, A](TSharedPtr<const FMRBgf> Bgf)
	{
		if (AMRAmbientFigure* Fig = Weak.Get(); Fig && Bgf.IsValid())
		{
			Fig->SetBitmap(Bgf, A, A, 1.f);
		}
	});
	Shots.Add(S.Id, S);
}

void FMRArenaPlayback::Sound(const FString& File, const FVector2D& At) const
{
	UWorld* W = World.Get();
	if (UMRAudioSubsystem* Audio = W ? W->GetSubsystem<UMRAudioSubsystem>() : nullptr)
	{
		Audio->ServerSound(File, World3(At), false, false, FMath::FRandRange(0.95f, 1.05f), 0);
	}
}

void FMRArenaPlayback::Event(const TSharedPtr<FJsonObject>& E)
{
	// Shards' ambientSounds.ts arenaEvent: the game's own sounds (its CC0 cheers and applause aside)
	const FString Type = E->GetStringField(TEXT("type"));
	const FVector2D At(E->GetNumberField(TEXT("x")), E->GetNumberField(TEXT("y")));
	bool bFemale = false;
	E->TryGetBoolField(TEXT("female"), bFemale);
	double Ouch = 0.0;
	E->TryGetNumberField(TEXT("ouch"), Ouch);
	const auto Cry = [this, &At, bFemale](int32 Level)
	{
		// player.kod GetOuchSound, in increasing order of agony
		if (Level >= 1 && Level <= 4)
		{
			Sound(FString::Printf(TEXT("ouch%s%d.ogg"), bFemale ? TEXT("f") : TEXT("m"), Level), At);
		}
	};
	if (Type == TEXT("swing"))
	{
		Sound(FString::Printf(TEXT("swswish%d.ogg"), FMath::RandRange(1, 3)), At);
		FString Hit;
		if (!E->TryGetStringField(TEXT("hit"), Hit) || Hit.IsEmpty())
		{
			Sound(TEXT("swordmis.ogg"), At);
			return;
		}
		// player.kod DoHitMessageSound: flesh, leather, metal
		Sound(FString::Printf(TEXT("%shit%db.ogg"), Hit == TEXT("metal") ? TEXT("m") : Hit == TEXT("leather") ? TEXT("l") : TEXT("f"), FMath::RandRange(1, 3)), At);
		Cry(int32(Ouch));
	}
	else if (Type == TEXT("cast"))
	{
		if (const FSpellArt* S = SpellArt(E->GetStringField(TEXT("spell"))))
		{
			Sound(S->Sound, At);
		}
	}
	else if (Type == TEXT("impact"))
	{
		if (E->GetStringField(TEXT("spell")) == TEXT("lightning"))
		{
			Sound(FString::Printf(TEXT("zaphit%d.ogg"), FMath::RandRange(1, 3)), At);
		}
		Cry(int32(Ouch));
	}
	else if (Type == TEXT("stuck") || Type == TEXT("scratch") || Type == TEXT("burn"))
	{
		Sound(TEXT("frying.ogg"), At);
		Cry(int32(Ouch));
	}
	else if (Type == TEXT("poisoned"))
	{
		Sound(TEXT("psncough.ogg"), At);
	}
	else if (Type == TEXT("cheer") && CheerMs <= 0.0)
	{
		CheerMs = FMath::FRandRange(2400.0, 6000.0);
		Sound(FMath::RandBool() ? TEXT("huzzah.ogg") : TEXT("arena.ogg"), At);
	}
	else if (Type == TEXT("death"))
	{
		Cry(4);
	}
}

void FMRArenaPlayback::Apply(const TSharedPtr<FJsonObject>& T)
{
	const double Now = T->GetNumberField(TEXT("now"));
	Phase = T->GetStringField(TEXT("phase"));
	double S = 0.0;
	if (T->TryGetNumberField(TEXT("season"), S))
	{
		Season = int32(S);
	}
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (T->TryGetArrayField(TEXT("ro"), List))
	{
		for (const TSharedPtr<FJsonValue>& R : *List) ReadRoster(R->AsObject());
	}
	if (T->TryGetArrayField(TEXT("add"), List))
	{
		for (const TSharedPtr<FJsonValue>& P : *List) AddFigure(P->AsObject(), Now);
	}
	if (T->TryGetArrayField(TEXT("mv"), List))
	{
		// [id, x, y, a, pose, ps, cf, hp, st, fade, fd, fav?]
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TArray<TSharedPtr<FJsonValue>>& M = V->AsArray();
			FFigure* F = M.Num() >= 11 ? Figures.Find(uint32(Num(M[0]))) : nullptr;
			if (!F)
			{
				continue;
			}
			const double K = FMath::Clamp((Now - F->At) / TickSpanMs, 0.0, 1.0);
			F->From = F->From + (F->To - F->From) * K;
			F->To = FVector2D(Num(M[1]), Num(M[2]));
			F->At = Now;
			F->Angle = int32(Num(M[3]));
			F->Pose = M[4]->AsString();
			F->Ps = int32(Num(M[5]));
			F->Hp = int32(Num(M[7]));
			F->St = int32(Num(M[8]));
			F->Fade = int32(Num(M[9]));
			F->Fd = int32(Num(M[10]));
			if (M.Num() > 11)
			{
				F->Fav = int32(Num(M[11]));
			}
			Dress(*F);
		}
	}
	if (T->TryGetArrayField(TEXT("del"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			if (FFigure* F = Figures.Find(uint32(Num(V))))
			{
				if (AMRAmbientFigure* A = F->Actor.Get()) A->Destroy();
				Figures.Remove(uint32(Num(V)));
			}
		}
	}
	if (T->TryGetArrayField(TEXT("fa"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List) AddField(V->AsObject());
	}
	if (T->TryGetArrayField(TEXT("fc"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TArray<TSharedPtr<FJsonValue>>& C = V->AsArray();
			if (FField* F = C.Num() == 2 ? Fields.Find(int32(Num(C[0]))) : nullptr)
			{
				F->G = int32(Num(C[1]));
				ShowField(*F);
			}
		}
	}
	if (T->TryGetArrayField(TEXT("fd"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			if (FField* F = Fields.Find(int32(Num(V))))
			{
				if (AMRAmbientFigure* A = F->Actor.Get()) A->Destroy();
				Fields.Remove(int32(Num(V)));
			}
		}
	}
	if (T->TryGetArrayField(TEXT("sh"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List) AddShot(V->AsObject());
	}
	if (T->TryGetArrayField(TEXT("say"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TArray<TSharedPtr<FJsonValue>>& L = V->AsArray();
			if (FFigure* F = L.Num() == 2 ? Figures.Find(uint32(Num(L[0]))) : nullptr)
			{
				F->Say = L[1]->AsString();
				F->SayMs = 3200.0 + F->Say.Len() * 40.0;
			}
		}
	}
	if (T->TryGetArrayField(TEXT("ev"), List))
	{
		for (const TSharedPtr<FJsonValue>& V : *List) Event(V->AsObject());
	}
}

void FMRArenaPlayback::Update(double NowMs, double DtMs)
{
	if (!bReady)
	{
		return;
	}
	if (bSync)
	{
		Offset = NowMs - (SyncTo - PlaybackDelayMs);
		bSync = false;
	}
	// a jitter buffer: keep the delay's worth of ticks queued, catching up or easing off slowly
	const double Newest = Queue.Num() ? Queue.Last()->GetNumberField(TEXT("now")) : Shown;
	const double Behind = Newest - (NowMs - Offset);
	if (Behind > PlaybackDelayMs * 3.0) Offset = NowMs - (Newest - PlaybackDelayMs);
	else if (Behind > PlaybackDelayMs * 1.5) Offset -= FMath::Min(DtMs * 0.1, 10.0);
	else if (Behind < PlaybackDelayMs * 0.5) Offset += FMath::Min(DtMs * 0.1, 10.0);
	const double T = NowMs - Offset;
	while (Queue.Num() && Queue[0]->GetNumberField(TEXT("now")) <= T)
	{
		const TSharedPtr<FJsonObject> Tick = Queue[0];
		Queue.RemoveAt(0);
		Apply(Tick);
	}
	Shown = T;
	CheerMs -= DtMs;
	for (TPair<uint32, FFigure>& Pair : Figures)
	{
		FFigure& F = Pair.Value;
		const double K = FMath::Clamp((T - F.At) / TickSpanMs, 0.0, 1.0);
		const FVector2D Roo = F.From + (F.To - F.From) * K;
		const FVector Feet = World3(Roo, F.bHasFeet ? &F.Feet : nullptr);
		const bool bMoving = K < 1.0 && FVector2D::Distance(F.From, F.To) > 4.0;
		const FVector Velocity = bMoving ? FVector(F.To.X - F.From.X, F.To.Y - F.From.Y, 0.0) * (MRUnits::CmPerSquare / 1024.0) / (TickSpanMs / 1000.0) : FVector::ZeroVector;
		F.Feet = Feet;
		F.bHasFeet = true;
		if (AMRAmbientFigure* A = F.Actor.Get())
		{
			A->Place(Feet, F.Angle * 360.f / 4096.f, Velocity);
		}
		if (!F.Say.IsEmpty() && (F.SayMs -= DtMs) <= 0.0)
		{
			F.Say.Reset();
		}
	}
	for (auto It = Shots.CreateIterator(); It; ++It)
	{
		FShot& S = It.Value();
		const double P = FMath::Clamp((T - S.T) / FMath::Max(1.0, S.FlightMs), 0.0, 1.0);
		if (AMRAmbientFigure* A = S.Actor.Get())
		{
			// at the casters' height
			const FVector Floor = World3(S.From + (S.To - S.From) * P);
			A->Place(Floor + FVector(0.0, 0.0, 100.0), FMath::RadiansToDegrees(FMath::Atan2(S.To.Y - S.From.Y, S.To.X - S.From.X)), FVector::ZeroVector);
		}
		if (P >= 1.0)
		{
			if (AMRAmbientFigure* A = S.Actor.Get()) A->Destroy();
			It.RemoveCurrent();
		}
	}
	for (auto It = Fields.CreateIterator(); It; ++It)
	{
		FField& F = It.Value();
		// the last moment: more see-through
		if (!F.bFading && F.End - T < FieldFadeMs)
		{
			F.bFading = true;
			if (AMRAmbientFigure* A = F.Actor.Get()) A->SetFade(0.5f);
		}
		if (T >= F.End + 500.0)
		{
			if (AMRAmbientFigure* A = F.Actor.Get()) A->Destroy();
			It.RemoveCurrent();
		}
	}
}

TArray<FMRArenaPlayback::FSpeech> FMRArenaPlayback::GetSpeech() const
{
	TArray<FSpeech> Out;
	for (const TPair<uint32, FFigure>& F : Figures)
	{
		if (!F.Value.Say.IsEmpty() && F.Value.Fd >= 0)
		{
			if (const AMRAmbientFigure* A = F.Value.Actor.Get())
			{
				Out.Add(FSpeech{A->GetSpeechAnchor(), F.Value.Say});
			}
		}
	}
	return Out;
}

uint32 FMRArenaPlayback::FigureNear(const FVector& Start, const FVector& Dir) const
{
	uint32 Best = 0;
	double BestOff = 60.0;
	for (const TPair<uint32, FFigure>& F : Figures)
	{
		const AMRAmbientFigure* A = F.Value.Actor.Get();
		if (!A || F.Value.Fd < 0)
		{
			continue;
		}
		const FVector Mid = A->GetFeet() + FVector(0.0, 0.0, 90.0);
		const double Along = FVector::DotProduct(Mid - Start, Dir);
		const double Off = FVector::Dist(Start + Dir * Along, Mid);
		if (Along > 0.0 && Along < 4000.0 && Off < BestOff)
		{
			BestOff = Off;
			Best = F.Key;
		}
	}
	return Best;
}

bool FMRArenaPlayback::Inspect(uint32 Id, FString& OutName, FString& OutText, FMRSpriteAppearance& OutLook) const
{
	// arenaView.ts describe
	const FFigure* F = Figures.Find(Id);
	if (!F || !ToAppearance(F->Look, false, OutLook))
	{
		return false;
	}
	const FRoster* R = F->Rid >= 0 ? Roster.Find(F->Rid) : nullptr;
	const int32 St = F->St;
	FString Doing;
	if (St & StWinner) Doing = TEXT("Savouring a victory");
	else if (St & StBeaten) Doing = TEXT("Getting over a beating, in the stands");
	else if (F->Role == TEXT("spectator")) Doing = TEXT("Watching the bouts");
	else if (St & StBlind) Doing = TEXT("Stumbling about, blinded");
	else if (St & StDazzled) Doing = TEXT("Stumbling about, dazzled");
	else if (St & StWebbed) Doing = TEXT("Stuck fast in a web");
	else if (Phase == TEXT("fight")) Doing = St & StPoisoned ? TEXT("Fighting, poisoned") : TEXT("Fighting a bout");
	else Doing = TEXT("Waiting for the next bout");
	FString Mood;
	if (St & StWinner) Mood = TEXT("Triumphant");
	else if (St & StBeaten) Mood = TEXT("Sore");
	else if (St & (StBlind | StDazzled | StWebbed)) Mood = TEXT("Furious");
	else if (F->Role != TEXT("spectator") && Phase == TEXT("fight")) Mood = F->Hp < F->Mhp / 4 ? TEXT("Desperate") : TEXT("Fierce");
	else Mood = Moods[NameHash(F->Name) % UE_ARRAY_COUNT(Moods)];
	const FString* ArchLabel = ArchLabels.Find(F->Arch);
	const FString Role = R ? (ArchLabel ? *ArchLabel : F->Arch) : F->Role == TEXT("fighter") ? TEXT("Arena fighter") : F->Role == TEXT("mage") ? TEXT("Arena mage") : TEXT("Spectator");
	OutName = F->Name.IsEmpty() ? FString(TEXT("Spectator")) : F->Name;
	TArray<FString> Lines = {Role, Doing};
	if (R)
	{
		if (!R->Weapon.IsEmpty()) Lines.Add(FString::Printf(TEXT("Favourite weapon: %s"), WeaponLabels.Contains(R->Weapon) ? *WeaponLabels[R->Weapon] : *R->Weapon));
		if (!R->Spell.IsEmpty()) Lines.Add(FString::Printf(TEXT("Favourite spell: %s"), SpellLabels.Contains(R->Spell) ? *SpellLabels[R->Spell] : *R->Spell));
		Lines.Add(FString::Printf(TEXT("Season %d: %d wins, %d losses, %d beaten"), Season, R->Wins, R->Losses, R->Kills));
		if (R->Champ.Num())
		{
			TArray<FString> Seasons;
			for (const int32 C : R->Champ) Seasons.Add(FString::FromInt(C));
			Lines.Add(FString::Printf(TEXT("Champion of season %s"), *FString::Join(Seasons, TEXT(", "))));
		}
		if (F->Role != TEXT("spectator")) Lines.Add(FString::Printf(TEXT("Health: %d of %d"), F->Hp, F->Mhp));
	}
	if (F->Fav >= 0)
	{
		if (const FFigure* Fav = Figures.Find(uint32(F->Fav))) Lines.Add(FString::Printf(TEXT("Cheering for: %s"), *Fav->Name));
	}
	Lines.Add(FString::Printf(TEXT("Mood: %s"), *Mood));
	OutText = FString::Join(Lines, TEXT("\n"));
	return true;
}

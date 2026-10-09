#include "Net/MRNetWorld.h"

#include "Misc/Paths.h"
#include "Net/MRProtocol.h"
#include "Net/MRResources.h"

bool FMRNetObject::IsPlayer() const
{
	return (Flags & MRMsg::OF_PLAYER) != 0;
}

bool FMRNetObject::IsCreature() const
{
	return (Flags & (MRMsg::OF_PLAYER | MRMsg::OF_ATTACKABLE | MRMsg::OF_NPC)) != 0;
}

bool MRNetRead::ObjectList(FMRReader& R, const FMRResourceTable& Res, TArray<FMRNetObject>& Out)
{
	Out.Reset();
	const int32 N = R.U16();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		if (!Object(R, Res, Out.AddDefaulted_GetRef()))
		{
			return false;
		}
	}
	return R.IsOk();
}

bool MRNetRead::IdList(FMRReader& R, TArray<uint32>& Out)
{
	Out.Reset();
	const int32 N = R.U16();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		Out.Add(MRMsg::PlainId(R.U32()));
	}
	return R.IsOk();
}

// ------------------------------------------------------------------------------ FMRNetWorld

void FMRNetWorld::ResetRoom()
{
	Objects.Reset();
	Users.Reset();
	PlayerOverlays.Reset();
}

void FMRNetWorld::ResetInventory()
{
	Inventory.Reset();
	bHasInventory = false;
	Using.Reset();
	ContentsOf = 0;
	Contents.Reset();
}

void FMRNetWorld::ResetAbilities()
{
	Spells.Reset();
	bHasSpells = false;
	Skills.Reset();
	bHasSkills = false;
	PlayerEnchantments.Reset();
	RoomEnchantments.Reset();
}

void FMRNetWorld::Reset()
{
	ResetRoom();
	ResetInventory();
	ResetAbilities();
	Shop = FMRNetShop();
	Trade = FMRNetTrade();
	Player = FMRNetPlayer();
	StatGroups.Reset();
	Effects.Reset();
	News = FMRNetNews();
	Guild = FMRNetGuild();
	GuildList = FMRNetGuildList();
	GuildCost = GuildSecretCost = 0;
	Preferences = 0;
	bHasPreferences = false;
}

// ------------------------------------------------------------------------------ FMRNetEffects

bool FMRNetEffects::Apply(FMRReader& R)
{
	// clientd3d effect.c PerformEffect: the limits are the original's
	const uint16 Effect = R.U16();
	const auto Ms = [&R](int32 Max, int32 Default) { const int32 V = R.I32(); return float(V > Max || V < 0 ? Default : V); };
	switch (Effect)
	{
	case MRMsg::EFFECT_INVERT: InvertMs = float(FMath::Max(0, R.I32())); break;
	case MRMsg::EFFECT_SHAKE: ShakeMs = float(FMath::Max(0, R.I32())); break;
	case MRMsg::EFFECT_PARALYZE: bParalyzed = true; break;
	case MRMsg::EFFECT_RELEASE: bParalyzed = false; break;
	case MRMsg::EFFECT_BLIND: bBlind = true; break;
	case MRMsg::EFFECT_SEE: bBlind = false; break;
	case MRMsg::EFFECT_PAIN: PainMs = Ms(10000, 10000); break;
	case MRMsg::EFFECT_WHITEOUT: WhiteoutMs = Ms(10000, 10000); break;
	case MRMsg::EFFECT_BLUR: BlurMs = FMath::Min(BlurMs + Ms(INT32_MAX, 10000), 200000.f); break;
	case MRMsg::EFFECT_WAVER: WaverMs = FMath::Min(WaverMs + Ms(INT32_MAX, 10000), 200000.f); break;
	case MRMsg::EFFECT_FLASHXLAT:
	{
		FlashMs = Ms(10000, 1000);
		const int32 Xlat = R.I32();
		FlashXlat = Xlat < 0 || Xlat > 0xFF ? 0 : uint32(Xlat);
		break;
	}
	case MRMsg::EFFECT_XLATOVERRIDE: XlatOverride = R.U32(); break;
	case MRMsg::EFFECT_RAINING:
	case MRMsg::EFFECT_SNOWING:
	case MRMsg::EFFECT_FIREWORKS: Weather = Effect; break;
	case MRMsg::EFFECT_CLEARWEATHER: Weather = 0; break;
	case MRMsg::EFFECT_SAND: bSand = true; break;
	case MRMsg::EFFECT_CLEARSAND: bSand = false; break;
	default: return false;
	}
	++Seq;
	return R.IsOk();
}

void FMRNetEffects::Tick(float Ms)
{
	for (float* T : { &PainMs, &WhiteoutMs, &InvertMs, &ShakeMs, &BlurMs, &WaverMs, &FlashMs })
	{
		*T = FMath::Max(0.f, *T - Ms);
	}
	if (FlashMs <= 0.f)
	{
		FlashXlat = 0;
	}
}

FMRNetStatGroup& FMRNetWorld::StatGroup(uint8 Group)
{
	if (FMRNetStatGroup* G = StatGroups.FindByPredicate([Group](const FMRNetStatGroup& E) { return E.Group == Group; }))
	{
		return *G;
	}
	FMRNetStatGroup& G = StatGroups.AddDefaulted_GetRef();
	G.Group = Group;
	StatGroups.Sort([](const FMRNetStatGroup& A, const FMRNetStatGroup& B) { return A.Group < B.Group; });
	return *StatGroups.FindByPredicate([Group](const FMRNetStatGroup& E) { return E.Group == Group; });
}

const FMRNetStatGroup* FMRNetWorld::FindStatGroup(uint8 Group) const
{
	return StatGroups.FindByPredicate([Group](const FMRNetStatGroup& E) { return E.Group == Group; });
}

// ------------------------------------------------------------------------------ readers

bool MRNetRead::Animation(FMRReader& R, FMRNetAnimation& Out)
{
	Out = FMRNetAnimation();
	Out.Type = R.U8();
	switch (Out.Type)
	{
	case MRMsg::ANIMATE_NONE:
		Out.Group = R.U16();
		break;
	case MRMsg::ANIMATE_CYCLE:
		Out.Period = R.U32();
		Out.GroupLow = R.U16();
		Out.GroupHigh = R.U16();
		Out.Group = Out.GroupLow;
		break;
	case MRMsg::ANIMATE_ONCE:
		Out.Period = R.U32();
		Out.GroupLow = R.U16();
		Out.GroupHigh = R.U16();
		Out.GroupFinal = R.U16();
		Out.Group = Out.GroupLow;
		break;
	default:
		break;  // the original client reads nothing more either (server.c ExtractAnimation)
	}
	return R.IsOk();
}

void MRNetRead::Palette(FMRReader& R, int32& OutXlat, int32& OutEffect)
{
	OutXlat = -1;
	OutEffect = -1;
	const uint8 Next = R.Peek();
	if (Next == MRMsg::ANIMATE_TRANSLATION)
	{
		R.U8();
		OutXlat = R.U8();
	}
	else if (Next == MRMsg::ANIMATE_EFFECT)
	{
		R.U8();
		OutEffect = R.U8();
	}
}

bool MRNetRead::Overlays(FMRReader& R, const FMRResourceTable& Res, TArray<FMRNetOverlay>& Out)
{
	Out.Reset();
	const int32 N = R.U8();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		FMRNetOverlay& O = Out.AddDefaulted_GetRef();
		const uint32 Icon = R.U32();
		O.Bgf = FPaths::GetBaseFilename(Res.Get(Icon)).ToLower();
		O.Hotspot = R.U8();
		int32 Effect;
		Palette(R, O.Xlat, Effect);
		Animation(R, O.Animation);
	}
	return R.IsOk();
}

bool MRNetRead::Object(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out)
{
	const uint32 RawId = R.U32();
	Out.Id = MRMsg::PlainId(RawId);
	Out.bNumber = MRMsg::IsNumberId(RawId);
	Out.Amount = Out.bNumber ? R.U32() : 0;
	Out.IconRsc = R.U32();
	Out.NameRsc = R.U32();
	Out.Flags = R.U32();
	Out.DrawEffect = R.U8();
	Out.MinimapFlags = R.U32();
	Out.NameColor = R.U32();
	Out.ObjectType = R.U8();
	Out.MoveOn = R.U8();
	Out.Light = FMRNetLight();
	Out.Light.Flags = R.U16();
	if (Out.Light.Flags != MRMsg::LIGHT_FLAG_NONE)
	{
		Out.Light.Intensity = R.U8();
		Out.Light.Color = R.U16();
	}
	Palette(R, Out.Xlat, Out.Effect);
	Animation(R, Out.Animation);
	Overlays(R, Res, Out.OverlayParts);
	Out.Icon = Res.Get(Out.IconRsc);
	Out.Name = Res.Get(Out.NameRsc);
	return R.IsOk();
}

bool MRNetRead::Spell(FMRReader& R, const FMRResourceTable& Res, FMRNetSpell& Out)
{
	Out = FMRNetSpell();
	Object(R, Res, Out.Object);
	Out.Targets = R.U8();
	Out.School = R.U8();
	return R.IsOk();
}

bool MRNetRead::SpellList(FMRReader& R, const FMRResourceTable& Res, TArray<FMRNetSpell>& Out)
{
	Out.Reset();
	const int32 N = R.U16();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		if (!Spell(R, Res, Out.AddDefaulted_GetRef()))
		{
			return false;
		}
	}
	return R.IsOk();
}

bool MRNetRead::BuyList(FMRReader& R, const FMRResourceTable& Res, FMRNetShop& Out)
{
	Out.Items.Reset();
	Object(R, Res, Out.Seller);
	const int32 N = R.U16();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		FMRNetForSale& E = Out.Items.AddDefaulted_GetRef();
		Object(R, Res, E.Object);
		E.Price = R.U32();
	}
	return R.IsOk();
}

bool MRNetRead::StatChange(FMRReader& R, FMRNetStatChange& Out)
{
	for (uint8& V : Out.Stats)
	{
		V = R.U8();
	}
	for (uint8& V : Out.Levels)
	{
		V = R.U8();
	}
	return R.IsOk();
}

bool MRNetRead::Projectile(FMRReader& R, const FMRResourceTable& Res, bool bRadius, FMRNetProjectile& Out)
{
	// clientd3d server.c HandleShoot / HandleRadiusShoot
	Out = FMRNetProjectile();
	Out.bRadius = bRadius;
	Out.IconRsc = R.U32();
	Out.Icon = Res.Get(Out.IconRsc);
	Palette(R, Out.Xlat, Out.Effect);
	Animation(R, Out.Animation);
	Out.Source = MRMsg::PlainId(R.U32());
	if (!bRadius)
	{
		Out.Dest = MRMsg::PlainId(R.U32());
	}
	Out.Speed = R.U8();
	Out.Flags = R.U16();
	if (bRadius)
	{
		Out.Range = R.U8();
		Out.Number = R.U8();
	}
	Out.Light.Flags = R.U16();
	if (Out.Light.Flags != MRMsg::LIGHT_FLAG_NONE)
	{
		Out.Light.Intensity = R.U8();
		Out.Light.Color = R.U16();
	}
	return R.IsOk();
}

bool MRNetRead::Hit(FMRReader R, const FMRResourceTable& Res, int32 Kind, FMRNetHit& Out)
{
	// battler.kod AssessHit's parameters, in order:
	//   we hit:     colour, weapon, damage word, article, name, damage, colour
	//   we're hit:  colour, article, name, weapon, damage word, damage, colour
	// a player's name comes as a string (%q), a monster's as a resource (%s)
	Out = FMRNetHit();
	Out.bDealt = Kind == 1 || Kind == 2;
	const bool bPlayer = Kind == 1 || Kind == 3;
	R.U32();  // colour
	if (Out.bDealt)
	{
		R.U32();  // weapon
		R.U32();  // damage word
	}
	R.U32();  // article
	Out.Name = bPlayer ? R.Str() : Res.Get(R.U32());
	if (!Out.bDealt)
	{
		R.U32();
		R.U32();
	}
	Out.Damage = R.I32();
	return R.IsOk() && !Out.Name.IsEmpty() && Kind >= 1 && Kind <= 4;
}

bool MRNetRead::ObjectNoLight(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out)
{
	const uint32 RawId = R.U32();
	Out.Id = MRMsg::PlainId(RawId);
	Out.bNumber = MRMsg::IsNumberId(RawId);
	Out.Amount = Out.bNumber ? R.U32() : 0;
	Out.IconRsc = R.U32();
	Out.NameRsc = R.U32();
	Out.Flags = R.U32();
	Out.DrawEffect = R.U8();
	Out.MinimapFlags = R.U32();
	Out.NameColor = R.U32();
	Out.ObjectType = R.U8();
	Out.MoveOn = R.U8();
	Out.Light = FMRNetLight();
	Palette(R, Out.Xlat, Out.Effect);
	Animation(R, Out.Animation);
	Overlays(R, Res, Out.OverlayParts);
	Out.Icon = Res.Get(Out.IconRsc);
	Out.Name = Res.Get(Out.NameRsc);
	return R.IsOk();
}

bool MRNetRead::Look(FMRReader& R, const FMRResourceTable& Res, FMRNetDescription& Out)
{
	Out = FMRNetDescription();
	if (!Object(R, Res, Out.Object))
	{
		return false;
	}
	Out.Flags = R.U8();
	if (!MRServerText::Format(Res, R.U32(), R, Out.Text))
	{
		return false;
	}
	if (Out.Flags & (MRMsg::DF_EDITABLE | MRMsg::DF_INSCRIBED))
	{
		if (!MRServerText::Format(Res, R.U32(), R, Out.Inscription))
		{
			return false;
		}
	}
	return R.IsOk();
}

bool MRNetRead::LookPlayer(FMRReader& R, const FMRResourceTable& Res, FMRNetDescription& Out)
{
	Out = FMRNetDescription();
	Out.bPlayer = true;
	if (!Object(R, Res, Out.Object))
	{
		return false;
	}
	Out.bEditable = R.U8() != 0;
	if (!MRServerText::Format(Res, R.U32(), R, Out.Text) || !MRServerText::Format(Res, R.U32(), R, Out.ExtraInfo))
	{
		return false;
	}
	Out.Url = R.Str();
	return R.IsOk();
}

bool MRNetRead::RoomObject(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out)
{
	if (!Object(R, Res, Out))
	{
		return false;
	}
	Out.KodRow = R.U16();
	Out.KodCol = R.U16();
	Out.Angle = R.U16();
	return Motion(R, Res, Out);
}

bool MRNetRead::Motion(FMRReader& R, const FMRResourceTable& Res, FMRNetObject& Out)
{
	int32 Effect;
	Palette(R, Out.MotionXlat, Effect);
	Animation(R, Out.MotionAnimation);
	Overlays(R, Res, Out.MotionOverlays);
	return R.IsOk();
}

bool MRNetRead::Player(FMRReader& R, const FMRResourceTable& Res, FMRNetPlayer& Out)
{
	Out.Id = MRMsg::PlainId(R.U32());
	Out.IconRsc = R.U32();
	Out.NameRsc = R.U32();
	Out.RoomObjectId = R.U32();
	Out.RoomFile = Res.Get(R.U32());
	Out.RoomName = Res.Get(R.U32());
	Out.RoomSecurity = R.U32();
	Out.AmbientLight = R.U8();
	Out.PlayerLight = R.U8();
	Out.BackgroundRsc = R.U32();
	Out.Background = Res.Get(Out.BackgroundRsc);
	Out.WadingSoundRsc = R.U32();
	Out.WadingSound = Res.Get(Out.WadingSoundRsc);
	Out.RoomFlags = R.U32();
	for (uint32& D : Out.Depth)
	{
		D = R.U32();
	}
	return R.IsOk();
}

void MRNetRead::SplitSubject(const FString& Text, FString& OutSubject, FString& OutBody)
{
	// mailfile.c: the subject is the first line after "Subject: " (the German client's "Betreff: ")
	OutSubject.Reset();
	OutBody = Text;
	for (const TCHAR* Lead : {TEXT("Subject: "), TEXT("Betreff: ")})
	{
		if (Text.StartsWith(Lead, ESearchCase::CaseSensitive))
		{
			const FString Rest = Text.Mid(FCString::Strlen(Lead));
			int32 End = INDEX_NONE;
			if (!Rest.FindChar(TEXT('\n'), End))
			{
				End = Rest.Len();
			}
			OutSubject = Rest.Left(End).TrimEnd();
			OutBody = Rest.Mid(End + 1);
			return;
		}
	}
}

bool MRNetRead::Mail(FMRReader& R, const FMRResourceTable& Res, FMRNetMail& Out, bool& bEnd)
{
	// mailnews.c HandleMail: u32 index, sender, u32 time, u16 recipients (0: no more mail), their names,
	// then a server message (user.kod: user_show_mail with the text)
	Out = FMRNetMail();
	Out.Index = R.U32();
	Out.From = R.Str();
	Out.Time = static_cast<int64>(R.U32()) + MRMsg::KodTimeOffset;
	const int32 N = R.U16();
	bEnd = N == 0;
	if (bEnd || !R.IsOk())
	{
		return R.IsOk();  // (the end's packet has two bytes more, which the original never reads)
	}
	if (N > 20)
	{
		return false;  // mail.h MAX_RECIPIENTS
	}
	for (int32 i = 0; i < N; ++i)
	{
		Out.To.Add(R.Str());
	}
	FString Text;
	if (!MRServerText::Format(Res, R.U32(), R, Text))
	{
		return false;
	}
	Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	SplitSubject(Text, Out.Subject, Out.Body);
	return R.IsOk();
}

bool MRNetRead::Articles(FMRReader& R, uint16& Group, uint8& Part, uint8& Parts, TArray<FMRNetArticle>& Out)
{
	// mailnews.c HandleArticles: u16 group, u8 part, u8 parts, u16 count, each u32 number, u32 time, poster, title
	Group = R.U16();
	Part = R.U8();
	Parts = R.U8();
	const int32 N = R.U16();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		FMRNetArticle& A = Out.AddDefaulted_GetRef();
		A.Num = R.U32();
		A.Time = static_cast<int64>(R.U32()) + MRMsg::KodTimeOffset;
		A.Poster = R.Str();
		A.Title = R.Str();
	}
	return R.IsOk();
}

bool MRNetRead::LookNewsgroup(FMRReader& R, const FMRResourceTable& Res, FMRNetNews& Out)
{
	// mailnews.c HandleLookNewsgroup: u16 group, u8 permission, the board (an object), its description
	Out.Group = R.U16();
	Out.Permission = R.U8();
	if (!Object(R, Res, Out.Board))
	{
		return false;
	}
	return MRServerText::Format(Res, R.U32(), R, Out.Description) && R.IsOk();
}

bool MRNetRead::LookupNames(FMRReader& R, TArray<uint32>& Out)
{
	// HandleLookupNames: u16 count, then each player's id (0: no such player)
	Out.Reset();
	const int32 N = R.U16();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		Out.Add(MRMsg::PlainId(R.U32()));
	}
	return R.IsOk();
}

bool MRNetRead::GuildInfo(FMRReader& R, FMRNetGuild& Out)
{
	// merintr.c HandleGuildInfo: name, u8 has password [password], u32 flags, u32 guild, five
	// (male, female) rank names, u32 current vote, u16 members: u32 id, name, u8 rank, u8 gender
	Out = FMRNetGuild();
	Out.Name = R.Str();
	Out.bHasPassword = R.U8() != 0;
	if (Out.bHasPassword)
	{
		Out.Password = R.Str();
	}
	Out.Flags = R.U32();
	Out.GuildId = MRMsg::PlainId(R.U32());
	for (int32 i = 0; i < MRMsg::GuildRanks; ++i)
	{
		Out.MaleRanks[i] = R.Str();
		Out.FemaleRanks[i] = R.Str();
	}
	Out.CurrentVote = MRMsg::PlainId(R.U32());
	const int32 N = R.U16();
	if (N > 400)
	{
		return false;  // guild.h MAX_GUILD_USERS
	}
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		FMRNetGuildMember& M = Out.Members.AddDefaulted_GetRef();
		M.Id = MRMsg::PlainId(R.U32());
		M.Name = R.Str();
		M.Rank = R.U8();
		M.Gender = R.U8();
	}
	Out.bValid = R.IsOk();
	return Out.bValid;
}

bool MRNetRead::GuildList(FMRReader& R, FMRNetGuildList& Out)
{
	// HandleGuildList: u16 guilds (u32 id, name), then four id lists: our allies, our enemies, and the
	// guilds that declared us their ally or enemy (user.kod UserGuildSendList)
	Out = FMRNetGuildList();
	const int32 N = R.U16();
	for (int32 i = 0; i < N && R.IsOk(); ++i)
	{
		const uint32 Id = MRMsg::PlainId(R.U32());
		Out.Guilds.Add({Id, R.Str()});
	}
	for (TArray<uint32>* L : {&Out.Allies, &Out.Enemies, &Out.DeclaredAllies, &Out.DeclaredEnemies})
	{
		const int32 M = R.U16();
		for (int32 i = 0; i < M && R.IsOk(); ++i)
		{
			L->Add(MRMsg::PlainId(R.U32()));
		}
	}
	Out.bValid = R.IsOk();
	return Out.bValid;
}

bool MRNetRead::RoomChange(uint8 Type, FMRReader& R, FMRNetRoomChange& Out)
{
	switch (Type)
	{
	case MRMsg::BP_SECTOR_MOVE:
		// server.c HandleSectorMove: type, sector, height, speed
		Out.Kind = FMRNetRoomChange::EKind::MoveSector;
		Out.Type = R.U8();
		Out.Id = R.U16();
		Out.Height = static_cast<int16>(R.U16());
		Out.Speed = R.U8();
		break;
	case MRMsg::BP_SECTOR_CHANGE:
		// HandleSectorChange: sector, depth, scroll
		Out.Kind = FMRNetRoomChange::EKind::ChangeSector;
		Out.Id = R.U16();
		Out.Depth = R.U8();
		Out.Scroll = R.U8();
		break;
	case MRMsg::BP_CHANGE_TEXTURE:
		// HandleChangeTexture: id, texture, flags
		Out.Kind = FMRNetRoomChange::EKind::ChangeTexture;
		Out.Id = R.U16();
		Out.Texture = R.U16();
		Out.Flags = R.U8();
		break;
	default:
		return false;
	}
	return R.IsOk();
}

bool MRNetRead::User(FMRReader& R, FMRNetUser& Out)
{
	Out.Id = MRMsg::PlainId(R.U32());
	Out.NameRsc = R.U32();
	Out.Name = R.Str();
	Out.Flags = R.U32();
	Out.DrawEffect = R.U8();
	Out.MinimapFlags = R.U32();
	Out.NameColor = R.U32();
	Out.ObjectType = R.U8();
	Out.MoveOn = R.U8();
	return R.IsOk();
}

bool MRNetRead::Stat(FMRReader& R, const FMRResourceTable& Res, FMRNetStat& Out)
{
	// merintr.c ExtractStatistic: num, name, type, then a numeric value (with limits when it's an
	// integer) or a list entry (object, value, icon)
	Out.Num = R.U8();
	Out.NameRsc = R.U32();
	Out.Name = Res.Get(Out.NameRsc);
	Out.Type = R.U8();
	if (Out.Type == FMRNetStat::Numeric)
	{
		Out.Tag = R.U8();
		const uint32 Value = R.U32();
		if (Out.Tag == 1)
		{
			Out.Value = static_cast<int32>(Value);
			Out.Min = R.I32();
			Out.Max = R.I32();
			Out.CurrentMax = R.I32();
		}
		else
		{
			Out.ValueText = Res.Get(Value);
		}
	}
	else if (Out.Type == FMRNetStat::List)
	{
		Out.ObjectId = MRMsg::PlainId(R.U32());
		Out.Value = R.I32();
		Out.IconRsc = R.U32();
		Out.Icon = Res.Get(Out.IconRsc);
	}
	else
	{
		return false;  // unknown type: the rest of the message can't be read
	}
	return R.IsOk();
}

uint32 MRNetRead::RooSecurity(const TArray<uint8>& RooBytes)
{
	// magic (4 bytes), version (u32), security (u32)
	if (RooBytes.Num() < 12)
	{
		return 0;
	}
	FMRReader R(RooBytes.GetData(), RooBytes.Num(), 8);
	return R.U32();
}

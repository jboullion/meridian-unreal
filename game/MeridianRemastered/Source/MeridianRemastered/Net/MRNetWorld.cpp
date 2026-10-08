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

void FMRNetWorld::Reset()
{
	ResetRoom();
	ResetInventory();
	Player = FMRNetPlayer();
	StatGroups.Reset();
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

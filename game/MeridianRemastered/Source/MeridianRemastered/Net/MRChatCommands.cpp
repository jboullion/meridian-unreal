#include "Net/MRChatCommands.h"

namespace
{
	using C = EMRChatCommand;

	struct FName2Command
	{
		const TCHAR* Name;
		C Command;
	};

	// merintr.c's commands in their order (the first a shortened name fits wins), with the German names;
	// mailnews.c adds "mail"
	const FName2Command Table[] = {
		{TEXT("say"), C::Say}, {TEXT("sagen"), C::Say}, {TEXT("broadcast"), C::Broadcast}, {TEXT("mitteilen"), C::Broadcast},
		{TEXT("emote"), C::Emote}, {TEXT("ego"), C::Emote}, {TEXT("who"), C::Who}, {TEXT("wer"), C::Who}, {TEXT("quit"), C::Quit},
		{TEXT("beenden"), C::Quit}, {TEXT("tell"), C::Tell}, {TEXT("telepathie"), C::Tell}, {TEXT("hel"), C::Hel}, {TEXT("hilf"), C::Hel},
		{TEXT("help"), C::Help}, {TEXT("hilfe"), C::Help}, {TEXT("use"), C::Use}, {TEXT("benutzen"), C::Use}, {TEXT("get"), C::Get},
		{TEXT("addgroup"), C::AddGroup}, {TEXT("gruppehinzu"), C::AddGroup}, {TEXT("agroup"), C::AddGroup}, {TEXT("put"), C::Put},
		{TEXT("ablegen"), C::Put}, {TEXT("delgroup"), C::DelGroup}, {TEXT("gruppel\u00F6schen"), C::DelGroup}, {TEXT("dgroup"), C::DelGroup},
		{TEXT("newgroup"), C::NewGroup}, {TEXT("neuegruppe"), C::NewGroup}, {TEXT("ngroup"), C::NewGroup}, {TEXT("buy"), C::Buy},
		{TEXT("kaufen"), C::Buy}, {TEXT("drop"), C::Drop}, {TEXT("wegwerfen"), C::Drop}, {TEXT("nehmen"), C::Get}, {TEXT("look"), C::Look},
		{TEXT("schauen"), C::Look}, {TEXT("offer"), C::Offer}, {TEXT("anbieten"), C::Offer}, {TEXT("cast"), C::Cast},
		{TEXT("zaubern"), C::Cast}, {TEXT("map"), C::Map}, {TEXT("karte"), C::Map}, {TEXT("wave"), C::Wave}, {TEXT("winken"), C::Wave},
		{TEXT("point"), C::Point}, {TEXT("deuten"), C::Point}, {TEXT("dance"), C::Dance}, {TEXT("tanzen"), C::Dance},
		{TEXT("alias"), C::Alias}, {TEXT("befehle"), C::Alias}, {TEXT("cmdalias"), C::Alias}, {TEXT("kurzbefehle"), C::Alias},
		{TEXT("rest"), C::Rest}, {TEXT("rasten"), C::Rest}, {TEXT("yell"), C::Yell}, {TEXT("rufen"), C::Yell}, {TEXT("stand"), C::Stand},
		{TEXT("aufstehen"), C::Stand}, {TEXT("suicid"), C::Suicid}, {TEXT("haraki"), C::Suicid}, {TEXT("suicide"), C::Suicide},
		{TEXT("harakiri"), C::Suicide}, {TEXT("neutral"), C::Neutral}, {TEXT("happy"), C::Happy}, {TEXT("gl\u00FCcklich"), C::Happy},
		{TEXT("sad"), C::Sad}, {TEXT("traurig"), C::Sad}, {TEXT("wry"), C::Wry}, {TEXT("grimmig"), C::Wry}, {TEXT("guild"), C::Guild},
		{TEXT("gilde"), C::Guild}, {TEXT("password"), C::Password}, {TEXT("passwort"), C::Password}, {TEXT("withdraw"), C::Withdraw},
		{TEXT("abheben"), C::Withdraw}, {TEXT("deposit"), C::Deposit}, {TEXT("einzahlen"), C::Deposit}, {TEXT("balance"), C::Balance},
		{TEXT("kontostand"), C::Balance}, {TEXT("group"), C::Group}, {TEXT("gruppen"), C::Group}, {TEXT("appeal"), C::Appeal},
		{TEXT("aufrufen"), C::Appeal}, {TEXT("tellguild"), C::TellGuild}, {TEXT("telgilde"), C::TellGuild},
		{TEXT("tguild"), C::TellGuild}, {TEXT("tgilde"), C::TellGuild},
		{TEXT("safety on"), C::SafetyOn}, {TEXT("sicherheit an"), C::SafetyOn}, {TEXT("safety off"), C::SafetyOff},
		{TEXT("sicherheit aus"), C::SafetyOff}, {TEXT("tempsafe on"), C::TempSafeOn}, {TEXT("tempsicherheit an"), C::TempSafeOn},
		{TEXT("tempsafe off"), C::TempSafeOff}, {TEXT("tempsicherheit aus"), C::TempSafeOff}, {TEXT("grouping on"), C::GroupingOn},
		{TEXT("gruppenbildung an"), C::GroupingOn}, {TEXT("grouping off"), C::GroupingOff}, {TEXT("gruppenbildung aus"), C::GroupingOff},
		{TEXT("autoloot on"), C::AutoLootOn}, {TEXT("autoloot an"), C::AutoLootOn}, {TEXT("autoloot off"), C::AutoLootOff},
		{TEXT("autoloot aus"), C::AutoLootOff}, {TEXT("autocombine on"), C::AutoCombineOn}, {TEXT("autocombine an"), C::AutoCombineOn},
		{TEXT("autocombine off"), C::AutoCombineOff}, {TEXT("autocombine aus"), C::AutoCombineOff},
		{TEXT("reagentbag on"), C::ReagentBagOn}, {TEXT("reagentbag an"), C::ReagentBagOn}, {TEXT("reagentbag off"), C::ReagentBagOff},
		{TEXT("reagentbag aus"), C::ReagentBagOff}, {TEXT("spellpower on"), C::SpellPowerOn}, {TEXT("spellpower an"), C::SpellPowerOn},
		{TEXT("spellpower off"), C::SpellPowerOff}, {TEXT("spellpower aus"), C::SpellPowerOff}, {TEXT("time"), C::Time},
		{TEXT("zeit"), C::Time}, {TEXT("mail"), C::Mail},
	};

	// ours, on top: shorthands players know from other games
	const FName2Command Shorthands[] = {{TEXT("em"), C::Emote}, {TEXT("me"), C::Emote}, {TEXT("bc"), C::Broadcast},
		{TEXT("t"), C::Tell}, {TEXT("w"), C::Tell}, {TEXT("gc"), C::TellGuild}, {TEXT("pickup"), C::Get}};

	const C WithWords[] = {C::Say, C::Broadcast, C::Emote, C::Yell, C::Tell, C::TellGuild, C::Appeal, C::Cast, C::AddGroup,
		C::DelGroup, C::NewGroup, C::Alias, C::Deposit, C::Withdraw};

	constexpr int32 MaxSay = 250;
	constexpr int32 MaxGroups = 30;      // groups.h MAX_NUMGROUPS
	constexpr int32 MaxGroupSize = 100;  // MAX_GROUPSIZE
	constexpr int32 MaxGroupName = 10;   // MAX_GROUPNAME

	/** The first word, and what follows it (spaces trimmed). */
	void FirstWord(const FString& Text, FString& OutWord, FString& OutRest)
	{
		const FString T = Text.TrimStart();
		int32 Space = INDEX_NONE;
		if (!T.FindChar(TEXT(' '), Space))
		{
			OutWord = T;
			OutRest.Reset();
			return;
		}
		OutWord = T.Left(Space);
		OutRest = T.Mid(Space + 1).TrimStart();
	}

	bool ByWholeName(const FString& Name, C& Out)
	{
		for (const FName2Command& E : Table)
		{
			if (Name.Equals(E.Name, ESearchCase::IgnoreCase))
			{
				Out = E.Command;
				return true;
			}
		}
		for (const FName2Command& E : Shorthands)
		{
			if (Name.Equals(E.Name, ESearchCase::IgnoreCase))
			{
				Out = E.Command;
				return true;
			}
		}
		return false;
	}

	/** An alias for the line's first word: the whole word, or (bPrefix) the only alias it starts. */
	FMRTypedLine::EKind MatchAlias(const TMap<FString, FString>& Aliases, const FString& Line, bool bPrefix, FString& OutLine)
	{
		FString Word, Rest;
		FirstWord(Line, Word, Rest);
		if (Word.IsEmpty())
		{
			return FMRTypedLine::EKind::Nothing;
		}
		const FString* Found = nullptr;
		int32 Starts = 0;
		for (const TPair<FString, FString>& A : Aliases)
		{
			if (A.Key.Equals(Word, ESearchCase::IgnoreCase))
			{
				Found = &A.Value;
				Starts = 1;
				break;
			}
			if (bPrefix && A.Key.StartsWith(Word, ESearchCase::IgnoreCase))
			{
				Found = &A.Value;
				++Starts;
			}
		}
		if (!Found)
		{
			return FMRTypedLine::EKind::Nothing;
		}
		if (Starts > 1)
		{
			return FMRTypedLine::EKind::AmbiguousAlias;
		}
		OutLine = Found->Replace(TEXT("~~"), *Rest);
		return FMRTypedLine::EKind::Command;
	}
}

FString MRChat::CommandName(EMRChatCommand Command)
{
	for (const FName2Command& E : Table)
	{
		if (E.Command == Command)
		{
			return E.Name;
		}
	}
	return FString();
}

bool MRChat::TakesWords(EMRChatCommand Command)
{
	for (const C W : WithWords)
	{
		if (W == Command)
		{
			return true;
		}
	}
	return false;
}

bool MRChat::FindCommand(const FString& Text, EMRChatCommand& Out, FString& OutArgs)
{
	FString Word, Rest;
	FirstWord(Text, Word, Rest);
	if (Word.IsEmpty())
	{
		return false;
	}
	// two-word names ("safety on"), whole
	FString Second, After;
	FirstWord(Rest, Second, After);
	if (!Second.IsEmpty() && ByWholeName(Word + TEXT(" ") + Second, Out))
	{
		OutArgs = After;
		return true;
	}
	OutArgs = Rest;
	if (ByWholeName(Word, Out))
	{
		return true;
	}
	for (const FName2Command& E : Table)
	{
		if (FString(E.Name).StartsWith(Word, ESearchCase::IgnoreCase))
		{
			Out = E.Command;
			return true;
		}
	}
	return false;
}

FMRTypedLine MRChat::Interpret(const FString& Line, const TMap<FString, FString>& Aliases, bool bOriginal)
{
	FMRTypedLine Out;
	FString Text = Line.TrimStartAndEnd();
	if (bOriginal && !Text.IsEmpty() && !Text.StartsWith(TEXT("/")) && !Text.StartsWith(TEXT(":")))
	{
		Text = TEXT("/") + Text;
	}
	// an alias may stand for another alias; a few rounds at most (no loops)
	for (int32 Round = 0; Round < 4; ++Round)
	{
		if (Text.IsEmpty())
		{
			Out.Kind = FMRTypedLine::EKind::Nothing;
			return Out;
		}
		if (Text.StartsWith(TEXT(":")))
		{
			Out.Command = EMRChatCommand::Emote;
			Out.Args = Text.Mid(1).TrimStart();
			Out.Kind = Out.Args.IsEmpty() ? FMRTypedLine::EKind::Nothing : FMRTypedLine::EKind::Command;
			return Out;
		}
		const bool bSlash = Text.StartsWith(TEXT("/"));
		const FString Body = bSlash ? Text.Mid(1).TrimStart() : Text;
		FString Expanded;
		const FMRTypedLine::EKind Alias = MatchAlias(Aliases, Body, bSlash, Expanded);
		if (Alias == FMRTypedLine::EKind::AmbiguousAlias)
		{
			Out.Kind = Alias;
			return Out;
		}
		if (Alias == FMRTypedLine::EKind::Command)
		{
			// an alias reads as a command line ("/" implied, as the original's)
			Text = Expanded.StartsWith(TEXT("/")) || Expanded.StartsWith(TEXT(":")) ? Expanded : TEXT("/") + Expanded;
			continue;
		}
		EMRChatCommand Command = EMRChatCommand::Say;
		FString Args;
		if (bSlash)
		{
			if (Body.IsEmpty())
			{
				Out.Kind = FMRTypedLine::EKind::Nothing;
			}
			else if (FindCommand(Body, Command, Args))
			{
				Out.Kind = FMRTypedLine::EKind::Command;
				Out.Command = Command;
				Out.Args = Args;
			}
			else
			{
				Out.Kind = FMRTypedLine::EKind::Unknown;
			}
			return Out;
		}
		// plain text: a command only by its whole name, and one without words only alone
		FString Word, Rest, Second, After;
		FirstWord(Body, Word, Rest);
		FirstWord(Rest, Second, After);
		if (!Second.IsEmpty() && After.IsEmpty() && ByWholeName(Word + TEXT(" ") + Second, Command))
		{
			Out.Kind = FMRTypedLine::EKind::Command;
			Out.Command = Command;
			return Out;
		}
		if (ByWholeName(Word, Command) && (TakesWords(Command) || Rest.IsEmpty()))
		{
			Out.Kind = FMRTypedLine::EKind::Command;
			Out.Command = Command;
			Out.Args = Rest;
			return Out;
		}
		Out.Kind = FMRTypedLine::EKind::Say;
		Out.Args = Body;
		return Out;
	}
	Out.Kind = FMRTypedLine::EKind::Unknown;
	return Out;
}

FString MRChat::FilterSay(const FString& Text)
{
	// say.c FilterSayMessage's limits (the profanity filter aside)
	FString Out;
	int32 Spaces = 0, CodeRun = 0, Codes = 0;
	for (int32 i = 0; i < Text.Len() && Out.Len() < MaxSay; ++i)
	{
		const TCHAR Ch = Text[i];
		if (Ch < 32)
		{
			continue;
		}
		const bool bCode = Ch == TEXT('~') || Ch == TEXT('`');
		if (Ch == TEXT(' ') || bCode)
		{
			++Spaces;
			if (bCode)
			{
				++CodeRun;
				++Codes;
			}
		}
		else
		{
			Spaces = CodeRun = 0;
		}
		const bool bKeep = Spaces <= 10 && CodeRun <= 4 && (!bCode || Codes <= 20);
		if (bCode)
		{
			// a code and its letter go together
			if (bKeep)
			{
				Out.AppendChar(Ch);
				if (i + 1 < Text.Len())
				{
					Out.AppendChar(Text[i + 1]);
				}
			}
			++i;
			continue;
		}
		if (bKeep)
		{
			Out.AppendChar(Ch);
		}
	}
	return Out.TrimStartAndEnd().IsEmpty() ? FString() : Out.TrimEnd();
}

TArray<FString> MRChat::SplitNames(const FString& Text)
{
	TArray<FString> Out;
	int32 i = 0;
	while (i < Text.Len())
	{
		const TCHAR Ch = Text[i];
		if (Ch == TEXT(' ') || Ch == TEXT(',') || Ch == TEXT('\t'))
		{
			++i;
			continue;
		}
		FString Name;
		if (Ch == TEXT('"'))
		{
			++i;
			while (i < Text.Len() && Text[i] != TEXT('"'))
			{
				Name.AppendChar(Text[i++]);
			}
			++i;  // the closing quote
		}
		else
		{
			while (i < Text.Len() && Text[i] != TEXT(' ') && Text[i] != TEXT(',') && Text[i] != TEXT('"') && Text[i] != TEXT('\t'))
			{
				Name.AppendChar(Text[i++]);
			}
		}
		Name.TrimStartAndEndInline();
		if (!Name.IsEmpty())
		{
			Out.Add(Name);
		}
	}
	return Out;
}

FString MRChat::FindGroup(const FGroups& Groups, const FString& Name, bool& bAmbiguous)
{
	bAmbiguous = false;
	FString Found;
	for (const TPair<FString, TArray<FString>>& G : Groups)
	{
		if (G.Key.Equals(Name, ESearchCase::IgnoreCase))
		{
			return G.Key;
		}
		if (G.Key.StartsWith(Name, ESearchCase::IgnoreCase))
		{
			bAmbiguous = !Found.IsEmpty();
			Found = G.Key;
		}
	}
	return bAmbiguous ? FString() : Found;
}

bool MRChat::ResolveTell(const FString& Args, const TArray<TPair<uint32, FString>>& Players, const FGroups& Groups, FTell& Out)
{
	Out = FTell();
	FString Rest = Args.TrimStartAndEnd();
	if (Rest.IsEmpty())
	{
		return false;
	}
	FString Name;
	if (Rest.StartsWith(TEXT("\"")))
	{
		int32 End = INDEX_NONE;
		Rest.Mid(1).FindChar(TEXT('"'), End);
		Name = End == INDEX_NONE ? Rest.Mid(1) : Rest.Mid(1, End);
		Rest = End == INDEX_NONE ? FString() : Rest.Mid(End + 2);
	}
	else
	{
		// the longest logged-on name the line starts with, as a whole word (names have spaces)
		int32 Best = 0;
		for (const TPair<uint32, FString>& P : Players)
		{
			const int32 L = P.Value.Len();
			if (L > Best && Rest.StartsWith(P.Value, ESearchCase::IgnoreCase) && (Rest.Len() == L || Rest[L] == TEXT(' ')))
			{
				Best = L;
			}
		}
		if (Best == 0)
		{
			int32 Space = INDEX_NONE;
			Best = Rest.FindChar(TEXT(' '), Space) ? Space : Rest.Len();
		}
		Name = Rest.Left(Best);
		Rest = Rest.Mid(Best);
	}
	Out.Text = Rest.TrimStartAndEnd();
	if (Name.IsEmpty() || Out.Text.IsEmpty())
	{
		return false;
	}
	auto ToGroup = [&](const FString& Group)
	{
		for (const FString& Member : Groups[Group])
		{
			for (const TPair<uint32, FString>& P : Players)
			{
				if (P.Value.Equals(Member, ESearchCase::IgnoreCase))
				{
					Out.Ids.AddUnique(P.Key);
				}
			}
		}
		if (Out.Ids.IsEmpty())
		{
			Out.Error = TEXT("No one from that group is currently logged in.");
		}
		return true;
	};
	for (const TPair<uint32, FString>& P : Players)
	{
		if (P.Value.Equals(Name, ESearchCase::IgnoreCase))
		{
			Out.Ids.Add(P.Key);
			return true;
		}
	}
	for (const TPair<FString, TArray<FString>>& G : Groups)
	{
		if (G.Key.Equals(Name, ESearchCase::IgnoreCase))
		{
			return ToGroup(G.Key);
		}
	}
	TArray<uint32> Starting;
	for (const TPair<uint32, FString>& P : Players)
	{
		if (P.Value.StartsWith(Name, ESearchCase::IgnoreCase))
		{
			Starting.Add(P.Key);
		}
	}
	if (Starting.Num() == 1)
	{
		Out.Ids = Starting;
		return true;
	}
	bool bAmbiguousGroup = false;
	const FString Group = FindGroup(Groups, Name, bAmbiguousGroup);
	if (!Group.IsEmpty())
	{
		return ToGroup(Group);
	}
	Out.Error = bAmbiguousGroup ? TEXT("That group name is ambiguous.")
		: Starting.Num() > 1 ? TEXT("That name is ambiguous.") : TEXT("No one with that name is logged on.");
	return true;
}

TArray<FString> MRChat::GroupNew(FGroups& Groups, const FString& Args)
{
	const TArray<FString> Names = SplitNames(Args);
	if (Names.IsEmpty())
	{
		TArray<FString> Keys;
		Groups.GetKeys(Keys);
		return {Keys.IsEmpty() ? TEXT("You have no groups defined.") : TEXT("Your groups: ") + FString::Join(Keys, TEXT(", "))};
	}
	if (Groups.Num() >= MaxGroups)
	{
		return {TEXT("You can't make more groups; delete some first.")};
	}
	const FString Name = Names[0].Left(MaxGroupName);
	for (const TPair<FString, TArray<FString>>& G : Groups)
	{
		if (G.Key.Equals(Name, ESearchCase::IgnoreCase))
		{
			return {TEXT("There is already a group with that name.")};
		}
	}
	Groups.Add(Name);
	return {FString::Printf(TEXT("Group %s created."), *Name)};
}

TArray<FString> MRChat::GroupAdd(FGroups& Groups, const FString& Args, TFunctionRef<bool(const FString&)> IsOnline)
{
	TArray<FString> Names = SplitNames(Args);
	if (Names.IsEmpty())
	{
		return GroupNew(Groups, FString());  // lists them
	}
	bool bAmbiguous = false;
	const FString Group = FindGroup(Groups, Names[0], bAmbiguous);
	if (Group.IsEmpty())
	{
		return {bAmbiguous ? TEXT("That group name is ambiguous.") : TEXT("There is no group matching that name.")};
	}
	TArray<FString>& Members = Groups[Group];
	if (Names.Num() == 1)
	{
		TArray<FString> Shown;
		for (const FString& M : Members)
		{
			Shown.Add(IsOnline(M) ? M + TEXT("*") : M);
		}
		return {FString::Printf(TEXT("The %s group (* = logged on): %s"), *Group, Shown.IsEmpty() ? TEXT("nobody") : *FString::Join(Shown, TEXT(", ")))};
	}
	int32 Added = 0;
	for (int32 i = 1; i < Names.Num(); ++i)
	{
		if (Members.Num() >= MaxGroupSize)
		{
			return {FString::Printf(TEXT("Added %d names; the %s group is full."), Added, *Group)};
		}
		if (!Members.ContainsByPredicate([&](const FString& M) { return M.Equals(Names[i], ESearchCase::IgnoreCase); }))
		{
			Members.Add(Names[i]);
			++Added;
		}
	}
	return {FString::Printf(TEXT("Added %d names to the %s group."), Added, *Group)};
}

TArray<FString> MRChat::GroupDelete(FGroups& Groups, const FString& Args)
{
	const TArray<FString> Names = SplitNames(Args);
	if (Names.IsEmpty())
	{
		return GroupNew(Groups, FString());
	}
	bool bAmbiguous = false;
	const FString Group = FindGroup(Groups, Names[0], bAmbiguous);
	if (Group.IsEmpty())
	{
		return {bAmbiguous ? TEXT("That group name is ambiguous.") : TEXT("There is no group matching that name.")};
	}
	if (Names.Num() == 1)
	{
		Groups.Remove(Group);
		return {FString::Printf(TEXT("Group %s deleted."), *Group)};
	}
	TArray<FString>& Members = Groups[Group];
	const int32 Before = Members.Num();
	for (int32 i = 1; i < Names.Num(); ++i)
	{
		Members.RemoveAll([&](const FString& M) { return M.Equals(Names[i], ESearchCase::IgnoreCase); });
	}
	return {FString::Printf(TEXT("Removed %d names from the %s group."), Before - Members.Num(), *Group)};
}

FString MRChat::DefineAlias(TMap<FString, FString>& Aliases, const FString& Args)
{
	FString Word, Rest;
	FirstWord(Args, Word, Rest);
	Word = Word.TrimEnd().ToLower();
	if (Word.EndsWith(TEXT("=")))
	{
		Word.LeftChopInline(1);
	}
	Rest.TrimStartAndEndInline();
	if (Rest.StartsWith(TEXT("=")))
	{
		Rest = Rest.Mid(1).TrimStart();  // "alias hi = say hello" reads as it looks
	}
	if (Word.IsEmpty())
	{
		return TEXT("Type: alias <word> <command>, or alias <word> to remove it.");
	}
	if (Rest.IsEmpty())
	{
		return Aliases.Remove(Word) ? FString::Printf(TEXT("Alias %s removed."), *Word) : FString::Printf(TEXT("There is no alias %s."), *Word);
	}
	Aliases.Add(Word, Rest);
	return FString::Printf(TEXT("Alias %s = %s"), *Word, *Rest);
}

TArray<FString> MRChat::ListAliases(const TMap<FString, FString>& Aliases)
{
	if (Aliases.IsEmpty())
	{
		return {TEXT("You have no aliases. Type: alias <word> <command>")};
	}
	TArray<FString> Out;
	for (const TPair<FString, FString>& A : Aliases)
	{
		Out.Add(FString::Printf(TEXT("%s = %s"), *A.Key, *A.Value));
	}
	Out.Sort();
	return Out;
}

int32 MRChat::FindByName(const TArray<FString>& Names, const FString& Typed)
{
	FString T = Typed.TrimStartAndEnd();
	T.RemoveFromStart(TEXT("\""));
	T.RemoveFromEnd(TEXT("\""));
	if (T.IsEmpty())
	{
		return INDEX_NONE;
	}
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		if (Names[i].Equals(T, ESearchCase::IgnoreCase))
		{
			return i;
		}
		if (Names[i].StartsWith(T, ESearchCase::IgnoreCase))
		{
			if (Found != INDEX_NONE)
			{
				Found = -2;
			}
			else
			{
				Found = i;
			}
		}
	}
	return Found;
}

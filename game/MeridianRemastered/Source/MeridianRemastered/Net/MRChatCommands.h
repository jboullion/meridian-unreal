#pragma once

#include "CoreMinimal.h"

/**
 * The commands typed in the chat line (docs/research/blakserv-protocol.md, "Chat and social"): the
 * original client's list (module/merintr merintr.c, command.c; mailnews.c adds "mail"), in its
 * order, which decides which command a shortened name means.
 */
enum class EMRChatCommand : uint8
{
	Say, Broadcast, Emote, Who, Quit, Tell, Hel, Help, Use, Get, AddGroup, Put, DelGroup, NewGroup, Buy, Drop, Look,
	Offer, Cast, Map, Wave, Point, Dance, Alias, Rest, Yell, Stand, Suicid, Suicide, Neutral, Happy, Sad, Wry, Guild,
	Password, Withdraw, Deposit, Balance, Group, Appeal, TellGuild, SafetyOn, SafetyOff, TempSafeOn, TempSafeOff,
	GroupingOn, GroupingOff, AutoLootOn, AutoLootOff, AutoCombineOn, AutoCombineOff, ReagentBagOn, ReagentBagOff,
	SpellPowerOn, SpellPowerOff, Time, Mail,
};

/** What a typed line means. */
struct FMRTypedLine
{
	enum class EKind : uint8
	{
		Nothing,         // an empty line
		Command,         // Command with Args (the words after its name)
		Say,             // Args said aloud
		Unknown,         // "/" and no command of that name: the original's "What?"
		AmbiguousAlias,  // the start of more than one alias
	};
	EKind Kind = EKind::Nothing;
	EMRChatCommand Command = EMRChatCommand::Say;
	FString Args;
};

namespace MRChat
{
	/**
	 * Our reading of a typed line, kept close to how people chat (the original made every line a
	 * command and needed "say" to talk):
	 * - ":text" emotes;
	 * - "/word ..." is a command, and the start of its name will do ("/b hi" broadcasts);
	 * - otherwise the line is said, unless its first word (or first two: "safety on") is a command's
	 *   whole name. A command that takes no words counts only when typed alone, so "drop it!" is said.
	 * - A command alias ("alias" command) runs in place of the line when its first word is the alias
	 *   (after "/", the start of one will do); "~~" in the alias takes the rest of the line.
	 * bOriginal (the option mr.Chat.OriginalTyping): every line is a command, as in the original
	 * client ("say" to speak), as if it began with "/".
	 */
	MERIDIANREMASTERED_API FMRTypedLine Interpret(const FString& Line, const TMap<FString, FString>& Aliases, bool bOriginal = false);

	/** The command a "/" line names: a whole name first, else the first (in the original's order) its word starts. */
	MERIDIANREMASTERED_API bool FindCommand(const FString& Text, EMRChatCommand& Out, FString& OutArgs);
	/** Its English name ("tellguild"). */
	MERIDIANREMASTERED_API FString CommandName(EMRChatCommand Command);
	/** Whether the words after it mean something (say, tell, cast...); the others count only typed alone. */
	MERIDIANREMASTERED_API bool TakesWords(EMRChatCommand Command);

	/**
	 * What may be said (say.c's limits): no control characters, at most 10 spaces or codes in a row,
	 * 4 codes in a row and 20 in all, at most 250 characters. Empty: nothing to say.
	 */
	MERIDIANREMASTERED_API FString FilterSay(const FString& Text);

	/** Names in a line: each one word, or "quoted with spaces", separated by spaces or commas. */
	MERIDIANREMASTERED_API TArray<FString> SplitNames(const FString& Text);

	/** Who a tell goes to (command.c CommandTell), and the text. Error says why not (a message for the player). */
	struct FTell
	{
		TArray<uint32> Ids;
		FString Text;
		FString Error;
	};
	/**
	 * "tell <name> <text>": a logged-on player by their whole name (names have spaces: the longest
	 * that fits), then a group by its whole name, then the only player whose name starts so, then a
	 * group by the start of its name. False when there is nothing to tell.
	 */
	MERIDIANREMASTERED_API bool ResolveTell(const FString& Args, const TArray<TPair<uint32, FString>>& Players,
		const TMap<FString, TArray<FString>>& Groups, FTell& Out);

	/** The tell groups (groups.c, kept by the client): "newgroup", "addgroup", "delgroup"; the messages to show. */
	using FGroups = TMap<FString, TArray<FString>>;
	MERIDIANREMASTERED_API TArray<FString> GroupNew(FGroups& Groups, const FString& Args);
	MERIDIANREMASTERED_API TArray<FString> GroupAdd(FGroups& Groups, const FString& Args, TFunctionRef<bool(const FString&)> IsOnline);
	MERIDIANREMASTERED_API TArray<FString> GroupDelete(FGroups& Groups, const FString& Args);
	/** A group by its whole name, or the only one its name starts; empty if none (bAmbiguous: more than one). */
	MERIDIANREMASTERED_API FString FindGroup(const FGroups& Groups, const FString& Name, bool& bAmbiguous);

	/** "alias word command": define (or with no command, remove) a command alias; the message to show. */
	MERIDIANREMASTERED_API FString DefineAlias(TMap<FString, FString>& Aliases, const FString& Args);
	/** The aliases as lines to show ("word = command"). */
	MERIDIANREMASTERED_API TArray<FString> ListAliases(const TMap<FString, FString>& Aliases);

	/** A name typed in full, or the only one it starts (spells for "cast"); INDEX_NONE if none, -2 if more than one. */
	MERIDIANREMASTERED_API int32 FindByName(const TArray<FString>& Names, const FString& Typed);
}

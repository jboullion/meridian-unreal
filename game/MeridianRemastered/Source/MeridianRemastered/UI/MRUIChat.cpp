// The chat line's commands (docs/adr/0012 M8): what UMRUISubsystem::RunChatLine does with a typed line.

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MeridianRemastered.h"
#include "Net/MRChatCommands.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "Net/MRNetWorldSubsystem.h"
#include "Net/MRProtocol.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRLookDialog.h"
#include "UI/SMRSocial.h"

namespace
{
	using C = EMRChatCommand;

	/** The server-kept option a command turns on or off (CF_*), and its name for the message. */
	bool PreferenceOf(C Command, uint32& OutFlag, bool& bOutOn, const TCHAR*& OutName)
	{
		struct FEntry { C On; C Off; uint32 Flag; bool bInverted; const TCHAR* Name; };
		// "safety on" clears CF_SAFETY_OFF (merintr command.c)
		const FEntry Entries[] = {
			{C::SafetyOn, C::SafetyOff, MRMsg::CF_SAFETY_OFF, true, TEXT("Safety")},
			{C::TempSafeOn, C::TempSafeOff, MRMsg::CF_TEMPSAFE, false, TEXT("Temporary safety after death")},
			{C::GroupingOn, C::GroupingOff, MRMsg::CF_GROUPING, false, TEXT("Grouping")},
			{C::AutoLootOn, C::AutoLootOff, MRMsg::CF_AUTOLOOT, false, TEXT("Auto-loot")},
			{C::AutoCombineOn, C::AutoCombineOff, MRMsg::CF_AUTOCOMBINE, false, TEXT("Auto-combine")},
			{C::ReagentBagOn, C::ReagentBagOff, MRMsg::CF_BAGS, false, TEXT("The reagent bag")},
			{C::SpellPowerOn, C::SpellPowerOff, MRMsg::CF_SPELLPOWER, false, TEXT("Spell power readout")},
		};
		for (const FEntry& E : Entries)
		{
			if (Command == E.On || Command == E.Off)
			{
				OutFlag = E.Flag;
				const bool bWantOn = Command == E.On;
				bOutOn = E.bInverted ? !bWantOn : bWantOn;  // the flag's value
				OutName = E.Name;
				return true;
			}
		}
		return false;
	}

	const TCHAR* HelpLines[] = {
		TEXT("Type to speak. Commands start with / (the start of a name will do):"),
		TEXT("  say, yell, broadcast (bc), emote (or :text), tell <name> <text> (t), tellguild (gc), appeal"),
		TEXT("  who, mail, guild, time, wave, point, dance, happy, sad, wry, neutral, rest, stand"),
		TEXT("  cast <spell>, get, look, use, buy, offer, deposit <n>, withdraw <n>, balance"),
		TEXT("  newgroup, addgroup, delgroup, group; alias <word> <command>"),
		TEXT("  safety on/off, tempsafe, grouping, autoloot, autocombine, reagentbag, spellpower on/off"),
	};
}

void UMRUISubsystem::RunChatLine(const FString& Line)
{
	UMRNetSubsystem* Net = GetNet();
	APlayerController* PC = GetPlayerController();
	UMRNetWorldSubsystem* World = PC && PC->GetWorld() ? PC->GetWorld()->GetSubsystem<UMRNetWorldSubsystem>() : nullptr;
	if (!Net || Net->GetPhase() != EMRNetPhase::InGame)
	{
		return;
	}
	if (World && !World->IsActive())
	{
		World = nullptr;
	}
	auto Msg = [Net](const FString& Text) { Net->AddGameMessage(Text); };
	const FMRTypedLine T = MRChat::Interpret(Line, Net->GetSocial().Aliases);
	switch (T.Kind)
	{
	case FMRTypedLine::EKind::Nothing:
		return;
	case FMRTypedLine::EKind::Unknown:
		Msg(TEXT("What?  (Type /help for the commands.)"));
		return;
	case FMRTypedLine::EKind::AmbiguousAlias:
		Msg(TEXT("That alias is ambiguous."));
		return;
	case FMRTypedLine::EKind::Say:
		Net->SayAs(MRMsg::SAY_NORMAL, T.Args);
		return;
	case FMRTypedLine::EKind::Command:
		break;
	}
	const FString& Args = T.Args;
	UE_LOG(LogMeridian, Log, TEXT("MRUI: command %s \"%s\""), *MRChat::CommandName(T.Command), *Args);
	uint32 Flag = 0;
	bool bOn = false;
	const TCHAR* OptionName = nullptr;
	if (PreferenceOf(T.Command, Flag, bOn, OptionName))
	{
		if (!Net->HasPreferences())
		{
			Msg(TEXT("Your options haven't come from the server yet; try again in a moment."));
			Net->RequestPreferences();
			return;
		}
		Net->SetPreferences(bOn ? (Net->GetPreferences() | Flag) : (Net->GetPreferences() & ~Flag));
		const bool bShownOn = Flag == MRMsg::CF_SAFETY_OFF ? !bOn : bOn;
		Msg(FString::Printf(TEXT("%s is now %s."), OptionName, bShownOn ? TEXT("on") : TEXT("off")));
		return;
	}
	MRChat::FGroups& Groups = Net->GetSocial().Groups;
	switch (T.Command)
	{
	case C::Say: Net->SayAs(MRMsg::SAY_NORMAL, Args); break;
	case C::Yell: Net->SayAs(MRMsg::SAY_YELL, Args); break;
	case C::Broadcast: Net->SayAs(MRMsg::SAY_EVERYONE, Args); break;
	case C::Emote: Net->SayAs(MRMsg::SAY_EMOTE, Args); break;
	case C::TellGuild: Net->SayAs(MRMsg::SAY_GUILD, Args); break;
	case C::Tell:
	{
		MRChat::FTell Tell;
		if (!MRChat::ResolveTell(Args, Net->GetUserNames(), Groups, Tell))
		{
			Msg(TEXT("Type: tell <name> <message>  (a name with spaces in \"quotes\")"));
		}
		else if (!Tell.Error.IsEmpty())
		{
			Msg(Tell.Error);
		}
		else
		{
			Net->SayTo(Tell.Ids, Tell.Text);
		}
		break;
	}
	case C::Appeal:
		if (Args.IsEmpty())
		{
			Msg(TEXT("Type: appeal <what's wrong>  (it goes to the guides)"));
		}
		else
		{
			Net->Appeal(Args);
		}
		break;
	case C::Who: SetWindowOpen(EMRWindow::Who, true); break;
	case C::Mail: SetWindowOpen(EMRWindow::Mail, true); break;
	case C::Guild:
		Net->GuildCommand(MRMsg::UC_REQ_GUILDINFO);
		SetWindowOpen(EMRWindow::Guild, true);
		break;
	case C::Time: Net->RequestTime(); break;
	case C::Quit: QuitGame(); break;
	case C::Hel: Msg(TEXT("Type /help for the commands.")); break;
	case C::Help:
		for (const TCHAR* L : HelpLines)
		{
			Msg(L);
		}
		break;
	case C::Wave: Net->DoAction(MRMsg::UA_WAVE); break;
	case C::Point: Net->DoAction(MRMsg::UA_POINT); break;
	case C::Dance: Net->DoAction(MRMsg::UA_DANCE); break;
	case C::Neutral: Net->DoAction(MRMsg::UA_NORMAL); break;
	case C::Happy: Net->DoAction(MRMsg::UA_HAPPY); break;
	case C::Sad: Net->DoAction(MRMsg::UA_SAD); break;
	case C::Wry: Net->DoAction(MRMsg::UA_WRY); break;
	case C::Rest:
	case C::Stand:
		if (World)
		{
			const bool bRest = T.Command == C::Rest;
			if (World->IsResting() == bRest)
			{
				Msg(bRest ? TEXT("You are already resting.") : TEXT("You are already standing."));
			}
			else
			{
				World->SetResting(bRest);
			}
		}
		break;
	case C::Cast:
	{
		if (Args.IsEmpty())
		{
			SetInventoryOpen(true);  // the spells are a tab of the inventory
			break;
		}
		TArray<FString> Names;
		for (const FMRNetSpell& S : Net->GetSpells())
		{
			Names.Add(S.Object.Name);
		}
		const int32 Found = MRChat::FindByName(Names, Args);
		if (Found == -2)
		{
			Msg(TEXT("That spell name is ambiguous."));
		}
		else if (Found == INDEX_NONE)
		{
			Msg(TEXT("You don't know a spell by that name."));
		}
		else if (World)
		{
			World->CastSpell(Net->GetSpells()[Found].Object.Id);
		}
		break;
	}
	case C::Get:
		if (World)
		{
			const TArray<uint32> Gettable = World->GettableAtAim();
			if (Gettable.Num() == 1)
			{
				PickChosen(EMRPickAction::Get, Gettable[0]);
			}
			else if (Gettable.Num() > 1)
			{
				ShowPicker(Gettable, EMRPickAction::Get);
			}
			else
			{
				Msg(TEXT("There's nothing there to pick up (aim at it, or press G)."));
			}
		}
		break;
	case C::Look:
		if (World && !World->LookAtTarget())
		{
			Msg(TEXT("Aim at something to look at it (or right-click it)."));
		}
		break;
	case C::Use:
		if (World && !World->UseAim())
		{
			Msg(TEXT("Aim at something to use it (or press F)."));
		}
		break;
	case C::Buy:
	case C::Offer:
	{
		const uint32 Id = World ? (World->GetTargetId() ? World->GetTargetId() : World->GetAimId()) : 0;
		if (!Id)
		{
			Msg(T.Command == C::Buy ? TEXT("Aim at a shopkeeper to buy.") : TEXT("Aim at someone to offer them something."));
		}
		else
		{
			DoObjectAction(T.Command == C::Buy ? EMRObjectAction::Buy : EMRObjectAction::Offer, Id);
		}
		break;
	}
	case C::Drop:
	case C::Put:
		SetInventoryOpen(true);
		Msg(T.Command == C::Drop ? TEXT("Drag a thing out of the inventory to drop it.") : TEXT("Putting things into containers isn't in this version yet."));
		break;
	case C::Map: Msg(TEXT("The map is at the top right; - and = zoom it.")); break;
	case C::Deposit:
	case C::Withdraw:
	{
		const int32 Amount = FCString::Atoi(*Args);
		if (Amount <= 0)
		{
			Msg(FString::Printf(TEXT("Type: %s <shillings>, next to a banker."), T.Command == C::Deposit ? TEXT("deposit") : TEXT("withdraw")));
		}
		else if (T.Command == C::Deposit)
		{
			Net->BankDeposit(Amount);
		}
		else
		{
			Net->BankWithdraw(Amount);
		}
		break;
	}
	case C::Balance: Net->BankBalance(); break;
	case C::NewGroup:
	case C::AddGroup:
	case C::DelGroup:
	case C::Group:
	{
		TArray<FString> Out;
		if (T.Command == C::NewGroup)
		{
			Out = MRChat::GroupNew(Groups, Args);
		}
		else if (T.Command == C::AddGroup)
		{
			const TArray<TPair<uint32, FString>> Users = Net->GetUserNames();
			Out = MRChat::GroupAdd(Groups, Args, [&Users](const FString& Name)
			{
				return Users.ContainsByPredicate([&Name](const TPair<uint32, FString>& U) { return U.Value.Equals(Name, ESearchCase::IgnoreCase); });
			});
		}
		else if (T.Command == C::DelGroup)
		{
			Out = MRChat::GroupDelete(Groups, Args);
		}
		else
		{
			Out = MRChat::GroupNew(Groups, FString());  // lists them
		}
		Net->SaveSocial();
		for (const FString& L : Out)
		{
			Msg(L);
		}
		break;
	}
	case C::Alias:
		if (Args.IsEmpty())
		{
			for (const FString& L : MRChat::ListAliases(Net->GetSocial().Aliases))
			{
				Msg(L);
			}
		}
		else
		{
			Msg(MRChat::DefineAlias(Net->GetSocial().Aliases, Args));
			Net->SaveSocial();
		}
		break;
	case C::Suicid:
	case C::Suicide:
		Msg(TEXT("Deleting a character comes with the character list's options (not in this version yet)."));
		break;
	case C::Password:
		Msg(TEXT("Changing your password comes with the Options (not in this version yet)."));
		break;
	default:
		break;
	}
}

#include "UI/SMROptions.h"

#include "Core/MRSettings.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/MRNetSubsystem.h"
#include "Net/MRNetWorld.h"
#include "Net/MRProtocol.h"
#include "Player/MRPlayerController.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRControls.h"
#include "UI/SMRHUD.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MROptions"

namespace
{
	const FLinearColor Dim(0.72f, 0.7f, 0.64f);
	const FLinearColor Bad(1.f, 0.55f, 0.45f);
	constexpr float WidthPx = 300.f;
	constexpr float LabelPx = 92.f;

	UGameUserSettings* Graphics()
	{
		return GEngine ? GEngine->GetGameUserSettings() : nullptr;
	}

	void SaveGraphics(bool bResolution)
	{
		if (UGameUserSettings* G = Graphics())
		{
			if (bResolution)
			{
				G->ApplyResolutionSettings(false);
			}
			G->ApplyNonResolutionSettings();
			G->SaveSettings();
		}
	}

	FText KeyText(const FKey& Key)
	{
		return Key.IsValid() ? Key.GetDisplayName(false) : LOCTEXT("NoKey", "-");
	}
}

SMROptionsDialog::~SMROptionsDialog()
{
	if (UMRNetSubsystem* N = Net())
	{
		N->OnPasswordChanged.Remove(PasswordHandle);
		N->OnPreferencesChanged.Remove(PrefsHandle);
	}
}

void SMROptionsDialog::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	Init(InUI, EMRWindow::Options);
	if (UMRNetSubsystem* N = Net())
	{
		PasswordHandle = N->OnPasswordChanged.AddSP(this, &SMROptionsDialog::OnPassword);
		PrefsHandle = N->OnPreferencesChanged.AddSP(this, &SMROptionsDialog::Rebuild);
	}
	// (built when opened: OnOpened; SharedThis isn't ready inside Construct)
}

void SMROptionsDialog::OnOpened()
{
	Capturing = NAME_None;
	Status.Reset();
	Rebuild();
}

void SMROptionsDialog::SetTab(const FString& InTab)
{
	Tab = InTab;
	Capturing = NAME_None;
	Status.Reset();
	Rebuild();
}

void SMROptionsDialog::OnPassword(bool bOk)
{
	Status = bOk ? TEXT("Password changed.") : TEXT("Your old password was wrong: the password is NOT changed.");
	if (bOk && OldPassword.IsValid())
	{
		OldPassword->SetText(FString());
		NewPassword->SetText(FString());
		NewPassword2->SetText(FString());
	}
}

// ------------------------------------------------------------------------------ rows

namespace
{
	/** A row: its label on the left, the controls after. */
	TSharedRef<SWidget> Row(UMRUIStyle* S, const FText& Label, const TSharedRef<SWidget>& Controls)
	{
		const float Px = S->Px();
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(LabelPx * Px)[MRUI::Label(S, Label, 9.5f)]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Controls];
	}

	/** Choices side by side; the one in use pressed. */
	TSharedRef<SWidget> Choices(UMRUISubsystem* Ui, const TArray<FText>& Names, TFunction<int32()> Current, TFunction<void(int32)> Choose,
		float Width = 44.f)
	{
		const float Px = Ui->GetStyle()->Px();
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f * Px, 0.f)
			[
				SNew(SMRTextButton, Ui).Text(Names[i]).TextSize(8.5f).MinWidth(Width)
					.bActive_Lambda([Current, i]() { return Current() == i; })
					.OnClicked(FSimpleDelegate::CreateLambda([Choose, i]() { Choose(i); }))
			];
		}
		return Box;
	}

	TSharedRef<SWidget> Toggle(UMRUISubsystem* Ui, TFunction<bool()> Get, TFunction<void(bool)> Set, bool bEnabled = true)
	{
		return Choices(Ui, {LOCTEXT("On", "On"), LOCTEXT("Off", "Off")}, [Get]() { return Get() ? 0 : 1; },
			[Set](int32 i) { Set(i == 0); }, 36.f)
			// (disabled: the server hasn't sent the options yet)
			;
	}

	TSharedRef<SWidget> CVarToggle(UMRUISubsystem* Ui, const TCHAR* Name)
	{
		return Toggle(Ui, [Name]() { return MRSettings::GetCVar(Name) != 0.f; }, [Name](bool b) { MRSettings::SetCVar(Name, b ? 1.f : 0.f); });
	}

	TSharedRef<SWidget> CVarSlider(UMRUISubsystem* Ui, const TCHAR* Name, int32 Min, int32 Max)
	{
		const float Px = Ui->GetStyle()->Px();
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMRSlider, Ui).Min(Min).Max(Max).Width(140.f).bTrack(true)
					.Value_Lambda([Name]() { return FMath::RoundToInt(MRSettings::GetCVar(Name)); })
					.OnChanged(SMRSlider::FOnValue::CreateLambda([Name](int32 V) { MRSettings::SetCVar(Name, static_cast<float>(V)); }))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f * Px, 0.f, 0.f, 0.f)
			[
				MRUI::Label(Ui->GetStyle(), TAttribute<FText>::CreateLambda([Name]() { return FText::AsNumber(FMath::RoundToInt(MRSettings::GetCVar(Name))); }), 9.5f, true)
			];
	}
}

// ------------------------------------------------------------------------------ tabs

TSharedRef<SWidget> SMROptionsDialog::MakeGraphics()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Add = [&](const FText& Label, const TSharedRef<SWidget>& W) { Box->AddSlot().AutoHeight().Padding(0.f, 1.5f * Px)[Row(S, Label, W)]; };
	Add(LOCTEXT("Quality", "Quality"), Choices(Ui, {LOCTEXT("Low", "Low"), LOCTEXT("Medium", "Medium"), LOCTEXT("High", "High"), LOCTEXT("Epic", "Epic")},
		[]() { const UGameUserSettings* G = Graphics(); return G ? G->GetOverallScalabilityLevel() : -1; },
		[](int32 i) { if (UGameUserSettings* G = Graphics()) { G->SetOverallScalabilityLevel(i); SaveGraphics(false); } }));
	Add(LOCTEXT("ViewDistance", "View distance"), Choices(Ui, {LOCTEXT("Near", "Near"), LOCTEXT("Medium", "Medium"), LOCTEXT("Far", "Far"), LOCTEXT("Farthest", "Farthest")},
		[]() { const UGameUserSettings* G = Graphics(); return G ? G->GetViewDistanceQuality() : -1; },
		[](int32 i) { if (UGameUserSettings* G = Graphics()) { G->SetViewDistanceQuality(i); SaveGraphics(false); } }));
	Add(LOCTEXT("Window", "Window"), Choices(Ui, {LOCTEXT("Windowed", "Window"), LOCTEXT("Borderless", "Borderless"), LOCTEXT("Fullscreen", "Full screen")},
		[]() { const UGameUserSettings* G = Graphics(); return !G ? -1 : G->GetFullscreenMode() == EWindowMode::Windowed ? 0 : G->GetFullscreenMode() == EWindowMode::WindowedFullscreen ? 1 : 2; },
		[](int32 i) { if (UGameUserSettings* G = Graphics()) { G->SetFullscreenMode(i == 0 ? EWindowMode::Windowed : i == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Fullscreen); SaveGraphics(true); } },
		54.f));
	// the screen's sizes, smaller and larger
	TSharedRef<SHorizontalBox> Res = SNew(SHorizontalBox);
	auto Step = [](int32 Dir)
	{
		UGameUserSettings* G = Graphics();
		TArray<FIntPoint> Sizes;
		if (!G || !UKismetSystemLibrary::GetSupportedFullscreenResolutions(Sizes) || Sizes.IsEmpty())
		{
			return;
		}
		const FIntPoint Now = G->GetScreenResolution();
		int32 At = Sizes.IndexOfByPredicate([Now](const FIntPoint& P) { return P == Now; });
		At = At == INDEX_NONE ? Sizes.Num() - 1 : FMath::Clamp(At + Dir, 0, Sizes.Num() - 1);
		G->SetScreenResolution(Sizes[At]);
		SaveGraphics(true);
	};
	Res->AddSlot().AutoWidth()[SNew(SMRTextButton, Ui).Text(FText::FromString(TEXT("<"))).TextSize(9.f).MinWidth(16.f).OnClicked(FSimpleDelegate::CreateLambda([Step]() { Step(-1); }))];
	Res->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4.f * Px, 0.f)
	[
		SNew(SBox).WidthOverride(64.f * Px).HAlign(HAlign_Center)
		[
			MRUI::Label(S, TAttribute<FText>::CreateLambda([]()
			{
				const UGameUserSettings* G = Graphics();
				const FIntPoint R = G ? G->GetScreenResolution() : FIntPoint::ZeroValue;
				return FText::FromString(FString::Printf(TEXT("%d x %d"), R.X, R.Y));
			}), 9.5f, true)
		]
	];
	Res->AddSlot().AutoWidth()[SNew(SMRTextButton, Ui).Text(FText::FromString(TEXT(">"))).TextSize(9.f).MinWidth(16.f).OnClicked(FSimpleDelegate::CreateLambda([Step]() { Step(1); }))];
	Add(LOCTEXT("Resolution", "Screen size"), Res);
	Add(LOCTEXT("VSync", "Vertical sync"), Toggle(Ui, []() { const UGameUserSettings* G = Graphics(); return G && G->IsVSyncEnabled(); },
		[](bool b) { if (UGameUserSettings* G = Graphics()) { G->SetVSyncEnabled(b); SaveGraphics(false); } }));
	Add(LOCTEXT("Frames", "Frame limit"), Choices(Ui, {LOCTEXT("F30", "30"), LOCTEXT("F60", "60"), LOCTEXT("F120", "120"), LOCTEXT("FNone", "None")},
		[]() { const UGameUserSettings* G = Graphics(); const float F = G ? G->GetFrameRateLimit() : 0.f; return F <= 0.f ? 3 : F <= 30.f ? 0 : F <= 60.f ? 1 : 2; },
		[](int32 i) { if (UGameUserSettings* G = Graphics()) { const float L[] = {30.f, 60.f, 120.f, 0.f}; G->SetFrameRateLimit(L[i]); SaveGraphics(false); } }, 34.f));
	Add(LOCTEXT("FOV", "Field of view"), CVarSlider(Ui, TEXT("mr.Camera.FOV"), 70, 110));
	return Box;
}

TSharedRef<SWidget> SMROptionsDialog::MakeSound()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Add = [&](const FText& Label, const TSharedRef<SWidget>& W) { Box->AddSlot().AutoHeight().Padding(0.f, 1.5f * Px)[Row(S, Label, W)]; };
	Add(LOCTEXT("Music", "Music"), CVarToggle(Ui, TEXT("mr.Audio.Music")));
	Add(LOCTEXT("MusicVolume", "Music volume"), CVarSlider(Ui, TEXT("mr.Audio.MusicVolume"), 0, 100));
	Add(LOCTEXT("Sounds", "Sounds"), CVarToggle(Ui, TEXT("mr.Audio.Sound")));
	Add(LOCTEXT("SoundVolume", "Sound volume"), CVarSlider(Ui, TEXT("mr.Audio.SoundVolume"), 0, 100));
	Add(LOCTEXT("Loops", "Room sounds"), CVarToggle(Ui, TEXT("mr.Audio.Loops")));
	Add(LOCTEXT("Random", "Random sounds"), CVarToggle(Ui, TEXT("mr.Audio.Random")));
	return Box;
}

TSharedRef<SWidget> SMROptionsDialog::MakeControls()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMROptionsDialog> Weak = SharedThis(this);
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FMRKeyBinding& B : MRKeys::All())
	{
		const FName Id = B.Id;
		List->AddSlot().AutoHeight().Padding(0.f, 0.5f * Px)
		[
			Row(S, B.Label,
				SNew(SMRTextButton, Ui).TextSize(9.f).MinWidth(90.f)
					.Text_Lambda([Weak, Id]()
					{
						const TSharedPtr<SMROptionsDialog> D = Weak.Pin();
						return D && D->Capturing == Id ? LOCTEXT("Press", "Press a key...") : KeyText(MRKeys::Get(Id));
					})
					.bActive_Lambda([Weak, Id]() { const TSharedPtr<SMROptionsDialog> D = Weak.Pin(); return D && D->Capturing == Id; })
					.OnClicked(FSimpleDelegate::CreateLambda([Weak, Id]()
					{
						if (TSharedPtr<SMROptionsDialog> D = Weak.Pin())
						{
							D->Capturing = D->Capturing == Id ? NAME_None : Id;
							D->Status = D->Capturing.IsNone() ? FString() : TEXT("Press the new key or mouse button (Esc: leave it).");
						}
					})))
		];
	}
	TSharedRef<SHorizontalBox> Presets = SNew(SHorizontalBox);
	auto Preset = [&](const FText& Text, bool bOriginal)
	{
		Presets->AddSlot().AutoWidth().Padding(0.f, 0.f, 3.f * Px, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(Text).TextSize(9.f).MinWidth(80.f).OnClicked(FSimpleDelegate::CreateLambda([Weak, bOriginal]()
			{
				MRKeys::UsePreset(bOriginal);
				if (TSharedPtr<SMROptionsDialog> D = Weak.Pin(); D && D->UI.IsValid())
				{
					if (AMRPlayerController* PC = Cast<AMRPlayerController>(D->UI->GetPlayerController()))
					{
						PC->RemapKeysIfChanged();
					}
					D->Status = bOriginal ? TEXT("The original client's keys: arrows walk and turn, Ctrl attacks.") : TEXT("This game's keys: WASD and the mouse.");
				}
			}))
		];
	};
	Preset(LOCTEXT("Modern", "Modern keys"), false);
	Preset(LOCTEXT("Original", "Original keys"), true);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(170.f * Px)[SNew(SScrollBox) + SScrollBox::Slot()[List]]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)[Presets];
}

void SMROptionsDialog::Capture(const FKey& Key)
{
	const FName Id = Capturing;
	Capturing = NAME_None;
	const FName Other = MRKeys::UsedBy(Key, Id);
	MRKeys::Set(Id, Key);
	if (!Other.IsNone())
	{
		MRKeys::Set(Other, EKeys::Invalid);  // one key, one use: the other loses it
		const FMRKeyBinding* B = MRKeys::All().FindByPredicate([Other](const FMRKeyBinding& E) { return E.Id == Other; });
		Status = FString::Printf(TEXT("%s was on %s: that has no key now."), *Key.GetDisplayName(false).ToString(), B ? *B->Label.ToString() : *Other.ToString());
	}
	else
	{
		Status.Reset();
	}
	if (AMRPlayerController* PC = UI.IsValid() ? Cast<AMRPlayerController>(UI->GetPlayerController()) : nullptr)
	{
		PC->RemapKeysIfChanged();
	}
}

FReply SMROptionsDialog::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	if (!Capturing.IsNone())
	{
		if (Event.GetKey() == EKeys::Escape)
		{
			Capturing = NAME_None;
			Status.Reset();
		}
		else
		{
			Capture(Event.GetKey());
		}
		return FReply::Handled();
	}
	return SMRSocialWindow::OnKeyDown(Geo, Event);
}

FReply SMROptionsDialog::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event)
{
	if (!Capturing.IsNone() && Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		Capture(Event.GetEffectingButton());  // (the left button clicks the dialog's buttons; bind it from the Original/Modern presets)
		return FReply::Handled();
	}
	return SMRSocialWindow::OnMouseButtonDown(Geo, Event);
}

TSharedRef<SWidget> SMROptionsDialog::MakeGame()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	UMRNetSubsystem* N = Net();
	TWeakObjectPtr<UMRNetSubsystem> WeakNet(N);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Add = [&](const FText& Label, const TSharedRef<SWidget>& W) { Box->AddSlot().AutoHeight().Padding(0.f, 1.5f * Px)[Row(S, Label, W)]; };
	if (N && N->HasPreferences())
	{
		// the server keeps these (UC_SEND_PREFERENCES; the safety flag is "off" when set)
		struct FPref { FText Label; uint32 Flag; bool bInverted; };
		const FPref Prefs[] = {
			{LOCTEXT("Safety", "Safety (no attacking players)"), MRMsg::CF_SAFETY_OFF, true},
			{LOCTEXT("TempSafe", "Safety after dying"), MRMsg::CF_TEMPSAFE, false},
			{LOCTEXT("Grouping", "Grouping"), MRMsg::CF_GROUPING, false},
			{LOCTEXT("AutoLoot", "Pick up loot"), MRMsg::CF_AUTOLOOT, false},
			{LOCTEXT("AutoCombine", "Combine reagents"), MRMsg::CF_AUTOCOMBINE, false},
			{LOCTEXT("Bags", "Use the reagent bag"), MRMsg::CF_BAGS, false},
			{LOCTEXT("SpellPower", "Show spell power"), MRMsg::CF_SPELLPOWER, false},
		};
		for (const FPref& P : Prefs)
		{
			const uint32 Flag = P.Flag;
			const bool bInv = P.bInverted;
			Add(P.Label, Toggle(Ui, [WeakNet, Flag, bInv]() { return WeakNet.IsValid() && (((WeakNet->GetPreferences() & Flag) != 0) != bInv); },
				[WeakNet, Flag, bInv](bool bOn)
				{
					if (WeakNet.IsValid())
					{
						const bool bSet = bOn != bInv;
						WeakNet->SetPreferences(bSet ? WeakNet->GetPreferences() | Flag : WeakNet->GetPreferences() & ~Flag);
					}
				}));
		}
	}
	else
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 1.5f * Px)[MRUI::Label(S, LOCTEXT("NoPrefs", "The server keeps more options: they show once you're in the game."), 9.f, false, Dim)];
	}
	Add(LOCTEXT("Damage", "Damage numbers"), CVarToggle(Ui, TEXT("mr.UI.DamageNumbers")));
	Add(LOCTEXT("OriginalTyping", "Every line a command"), CVarToggle(Ui, TEXT("mr.Chat.OriginalTyping")));
	Box->AddSlot().AutoHeight()[MRUI::Label(S, LOCTEXT("OriginalTypingHelp", "(as the original client: \"say\" to speak)"), 8.5f, false, Dim)];
	return Box;
}

TSharedRef<SWidget> SMROptionsDialog::MakeChat()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	UMRNetSubsystem* N = Net();
	TWeakObjectPtr<UMRNetSubsystem> WeakNet(N);
	TWeakPtr<SMROptionsDialog> Weak = SharedThis(this);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (!N || N->GetPhase() != EMRNetPhase::InGame)
	{
		Box->AddSlot().AutoHeight()[MRUI::Label(S, LOCTEXT("NoChar", "The quick chat is kept per character: it shows once you're in the game."), 9.f, false, Dim)];
		return Box;
	}
	const FMRSocial& Soc = N->GetSocial();
	Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f * Px)
	[
		MRUI::Label(S, LOCTEXT("QuickHelp", "The function keys run these lines (\"Run\") or start them in the chat line."), 8.5f, false, Dim)
	];
	QuickFields.Reset();
	for (int32 i = 0; i < 12; ++i)
	{
		TSharedPtr<SMRTextField> Field;
		SAssignNew(Field, SMRTextField, Ui).Width(150.f).MaxLength(120).InitialText(FText::FromString(Soc.QuickChat[i]));
		QuickFields.Add(Field);
		if (i == 9)
		{
			continue;  // F10 opens the menu
		}
		Box->AddSlot().AutoHeight().Padding(0.f, 0.5f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(26.f * Px)[MRUI::Label(S, FText::FromString(FString::Printf(TEXT("F%d"), i + 1)), 9.5f, true)]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f * Px, 0.f)[Field.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMRTextButton, Ui).Text(LOCTEXT("Run", "Run")).TextSize(8.5f).MinWidth(30.f)
					.bActive_Lambda([WeakNet, i]() { return WeakNet.IsValid() && WeakNet->GetSocial().QuickRun[i]; })
					.OnClicked(FSimpleDelegate::CreateLambda([WeakNet, i]()
					{
						if (WeakNet.IsValid())
						{
							WeakNet->GetSocial().QuickRun[i] = !WeakNet->GetSocial().QuickRun[i];
							WeakNet->SaveSocial();
						}
					}))
			]
		];
	}
	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget];
	Buttons->AddSlot().AutoWidth().Padding(2.f * Px, 0.f, 0.f, 0.f)
	[
		SNew(SMRTextButton, Ui).Text(LOCTEXT("Defaults", "The original's")).TextSize(9.f).MinWidth(60.f).OnClicked(FSimpleDelegate::CreateLambda([Weak, WeakNet]()
		{
			if (WeakNet.IsValid())
			{
				WeakNet->GetSocial().DefaultQuickChat();
				WeakNet->SaveSocial();
			}
			if (TSharedPtr<SMROptionsDialog> D = Weak.Pin())
			{
				D->Rebuild();
			}
		}))
	];
	Buttons->AddSlot().AutoWidth().Padding(2.f * Px, 0.f, 0.f, 0.f)
	[
		SNew(SMRTextButton, Ui).Text(LOCTEXT("SaveQuick", "Keep")).TextSize(9.f).MinWidth(44.f).OnClicked(FSimpleDelegate::CreateLambda([Weak, WeakNet]()
		{
			TSharedPtr<SMROptionsDialog> D = Weak.Pin();
			if (D && WeakNet.IsValid())
			{
				for (int32 i = 0; i < D->QuickFields.Num() && i < 12; ++i)
				{
					WeakNet->GetSocial().QuickChat[i] = D->QuickFields[i]->GetText().TrimStartAndEnd();
				}
				WeakNet->SaveSocial();
				D->Status = TEXT("The quick chat is kept.");
			}
		}))
	];
	Box->AddSlot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)[Buttons];
	return Box;
}

TSharedRef<SWidget> SMROptionsDialog::MakeAccount()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	UMRNetSubsystem* N = Net();
	TWeakPtr<SMROptionsDialog> Weak = SharedThis(this);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (!N || N->GetPhase() != EMRNetPhase::InGame)
	{
		Box->AddSlot().AutoHeight()[MRUI::Label(S, LOCTEXT("NoAccount", "Log in to change the account."), 9.f, false, Dim)];
		return Box;
	}
	SAssignNew(OldPassword, SMRTextField, Ui).Width(130.f).MaxLength(30).bPassword(true);
	SAssignNew(NewPassword, SMRTextField, Ui).Width(130.f).MaxLength(30).bPassword(true);
	SAssignNew(NewPassword2, SMRTextField, Ui).Width(130.f).MaxLength(30).bPassword(true);
	SAssignNew(DeletePassword, SMRTextField, Ui).Width(130.f).MaxLength(30).bPassword(true);
	auto Add = [&](const FText& Label, const TSharedRef<SWidget>& W) { Box->AddSlot().AutoHeight().Padding(0.f, 1.f * Px)[Row(S, Label, W)]; };
	Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f * Px)[MRUI::Label(S, LOCTEXT("PasswordTitle", "Change the password"), 10.f, true)];
	Add(LOCTEXT("Old", "Old password"), OldPassword.ToSharedRef());
	Add(LOCTEXT("New", "New password"), NewPassword.ToSharedRef());
	Add(LOCTEXT("Again", "Again"), NewPassword2.ToSharedRef());
	Box->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 2.f * Px)
	[
		SNew(SMRTextButton, Ui).Text(LOCTEXT("Change", "Change")).TextSize(9.f).MinWidth(60.f).OnClicked(FSimpleDelegate::CreateLambda([Weak]()
		{
			TSharedPtr<SMROptionsDialog> D = Weak.Pin();
			if (!D || !D->Net())
			{
				return;
			}
			if (D->NewPassword->GetText() != D->NewPassword2->GetText())
			{
				D->Status = TEXT("The new password and its repeat differ.");
				return;
			}
			const FString Why = D->Net()->ChangePassword(D->OldPassword->GetText(), D->NewPassword->GetText());
			D->Status = Why.IsEmpty() ? TEXT("Asking the server...") : Why;
		}))
	];
	const FString Name = N->GetSelf() ? N->GetSelf()->Name : FString();
	Box->AddSlot().AutoHeight().Padding(0.f, 6.f * Px, 0.f, 2.f * Px)[MRUI::Label(S, LOCTEXT("DeleteTitle", "Delete this character"), 10.f, true, Bad)];
	TSharedRef<STextBlock> Warn = MRUI::Label(S, FText::Format(LOCTEXT("DeleteWarn",
		"This destroys {0} for good: the server makes the slot a new character. Type your password to confirm."), FText::FromString(Name)), 8.5f, false, Dim);
	Warn->SetAutoWrapText(true);
	Box->AddSlot().AutoHeight()[Warn];
	Add(LOCTEXT("Password", "Your password"), DeletePassword.ToSharedRef());
	Box->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 2.f * Px)
	[
		SNew(SMRTextButton, Ui).Text(LOCTEXT("Delete", "Delete")).TextSize(9.f).MinWidth(60.f).OnClicked(FSimpleDelegate::CreateLambda([Weak]()
		{
			TSharedPtr<SMROptionsDialog> D = Weak.Pin();
			if (!D || !D->Net())
			{
				return;
			}
			if (!D->Net()->IsLoginPassword(D->DeletePassword->GetText()))
			{
				D->Status = TEXT("That isn't your password: nothing was deleted.");
				return;
			}
			D->Net()->DeleteCharacter();
			D->Status = TEXT("Asking the server...");
		}))
	];
	return Box;
}

// ------------------------------------------------------------------------------ the window

void SMROptionsDialog::Rebuild()
{
	UMRUISubsystem* Ui = UI.Get();
	if (!Ui || !Ui->GetStyle())
	{
		return;
	}
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TWeakPtr<SMROptionsDialog> Weak = SharedThis(this);
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (const TCHAR* T : {TEXT("Graphics"), TEXT("Sound"), TEXT("Controls"), TEXT("Game"), TEXT("Chat"), TEXT("Account")})
	{
		const FString Name = T;
		Tabs->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f * Px, 0.f)
		[
			SNew(SMRTextButton, Ui).Text(FText::FromString(Name)).TextSize(9.f).MinWidth(44.f).bActive(Tab == Name)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak, Name]() { if (TSharedPtr<SMROptionsDialog> D = Weak.Pin()) D->SetTab(Name); }))
		];
	}
	const TSharedRef<SWidget> Page = Tab == TEXT("Sound") ? MakeSound() : Tab == TEXT("Controls") ? MakeControls() : Tab == TEXT("Game") ? MakeGame()
		: Tab == TEXT("Chat") ? MakeChat() : Tab == TEXT("Account") ? MakeAccount() : MakeGraphics();
	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			MRUI::Label(S, TAttribute<FText>::CreateLambda([Weak]() { const TSharedPtr<SMROptionsDialog> D = Weak.Pin(); return FText::FromString(D ? D->Status : FString()); }),
				8.5f, false, FLinearColor(1.f, 0.85f, 0.5f))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SMRTextButton, Ui).Text(LOCTEXT("Close", "Close")).TextSize(9.f).MinWidth(44.f)
				.OnClicked(FSimpleDelegate::CreateLambda([Weak]() { if (TSharedPtr<SMROptionsDialog> D = Weak.Pin()) D->Close(); }))
		];
	SetFrame(LOCTEXT("Title", "Options"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f * Px)[Tabs]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).MinDesiredHeight(190.f * Px)[Page]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f * Px, 0.f, 0.f)[Buttons],
		WidthPx);
}

#undef LOCTEXT_NAMESPACE

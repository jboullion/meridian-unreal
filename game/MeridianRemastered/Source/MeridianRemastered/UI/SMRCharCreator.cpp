#include "UI/SMRCharCreator.h"

#include "Character/MRSpriteData.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/MRNetSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/MRGameData.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRControls.h"
#include "UI/SMRHUD.h"
#include "UI/SMRInventoryScreen.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Zones/MRZoneSubsystem.h"

#define LOCTEXT_NAMESPACE "MRCharCreator"

namespace
{
	constexpr float PageW = 360.f;   // the page's inside, original pixels
	constexpr float PageH = 214.f;
	constexpr float ListW = 136.f;
	constexpr float ListH = 128.f;
	constexpr float BarH = 10.5f;    // the stat sliders and the points-left bars
	constexpr float InfoScale = 1.3f;
	/** Information text (prompts, descriptions, captions): 30% over the controls' size. */
	float InfoSize(float Size) { return Size * InfoScale; }

	using MRUI::Label;

	FLinearColor Dim() { return FLinearColor(0.85f, 0.82f, 0.74f); }


	const TCHAR* StatKeys[] = {TEXT("Might"), TEXT("Intellect"), TEXT("Stamina"), TEXT("Agility"), TEXT("Mysticism"), TEXT("Aim")};
}

SMRCharCreator::~SMRCharCreator()
{
	if (UMRNetSubsystem* Net = GetNet())
	{
		Net->OnCharInfo.Remove(CharInfoHandle);
		Net->OnCreateFailed.Remove(CreateFailedHandle);
	}
	if (UI.IsValid())
	{
		UI->SetCreatorCapturing(false);
	}
}

UMRNetSubsystem* SMRCharCreator::GetNet() const
{
	const ULocalPlayer* LP = UI.IsValid() ? UI->GetLocalPlayer() : nullptr;
	const UGameInstance* GI = LP ? LP->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMRNetSubsystem>() : nullptr;
}

const FMRCharInfo& SMRCharCreator::Info() const
{
	static const FMRCharInfo Empty;
	const UMRNetSubsystem* Net = GetNet();
	return Net ? Net->GetCharInfo() : Empty;
}

void SMRCharCreator::LoadConfig()
{
	FString Text;
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("ui"), TEXT("char_create.json"));
	if (FFileHelper::LoadFileToString(Text, *Path))
	{
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config);
	}
	if (!Config.IsValid())
	{
		Config = MakeShared<FJsonObject>();
	}
}

FString SMRCharCreator::Prompt(const TCHAR* Key) const
{
	const TSharedPtr<FJsonObject>* P;
	FString S;
	if (Config.IsValid() && Config->TryGetObjectField(TEXT("prompts"), P))
	{
		(*P)->TryGetStringField(Key, S);
	}
	return S;
}

void SMRCharCreator::Construct(const FArguments& InArgs, UMRUISubsystem* InUI)
{
	UI = InUI;
	LoadConfig();
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->Px();
	const FLinearColor Heading = S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f));
	if (UMRNetSubsystem* Net = GetNet())
	{
		CharInfoHandle = Net->OnCharInfo.AddLambda([this]() { Begin(); });
		CreateFailedHandle = Net->OnCreateFailed.AddLambda([this]() { SetTab(ETab::Name); });
	}

	FString Title = TEXT("Customize your character");
	Config->TryGetStringField(TEXT("title"), Title);
	TArray<FString> TabNames = {TEXT("Name"), TEXT("Appearance"), TEXT("Statistics"), TEXT("Spells"), TEXT("Skills")};
	const TArray<TSharedPtr<FJsonValue>>* TabsJson;
	if (Config->TryGetArrayField(TEXT("tabs"), TabsJson))
	{
		for (int32 i = 0; i < TabsJson->Num() && i < TabNames.Num(); ++i)
		{
			TabNames[i] = (*TabsJson)[i]->AsString();
		}
	}
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (int32 i = 0; i < static_cast<int32>(ETab::Count); ++i)
	{
		const ETab T = static_cast<ETab>(i);
		Tabs->AddSlot().FillWidth(1.f).Padding(i > 0 ? 1.f * Px : 0.f, 0.f, 0.f, 0.f)
		[
			SNew(SMRTextButton, InUI).Text(FText::FromString(TabNames[i])).TextSize(10.f).MinWidth(54.f)
				.bActive_Lambda([this, T]() { return Tab == T; })
				.OnClicked_Lambda([this, T]() { SetTab(T); })
		];
	}

	TSharedRef<SOverlay> Pages = SNew(SOverlay);
	const TSharedRef<SWidget> PageWidgets[] = {MakeNamePage(), MakeAppearancePage(), MakeStatsPage(), MakeAbilityPage(false), MakeAbilityPage(true)};
	for (int32 i = 0; i < UE_ARRAY_COUNT(PageWidgets); ++i)
	{
		const ETab T = static_cast<ETab>(i);
		Pages->AddSlot()
		[
			SNew(SBox).Visibility_Lambda([this, T]() { return Tab == T ? EVisibility::Visible : EVisibility::Hidden; })
			[
				PageWidgets[i]
			]
		];
	}

	auto Error = TAttribute<FText>::CreateLambda([this]()
	{
		const UMRNetSubsystem* Net = GetNet();
		if (!LocalError.IsEmpty())
		{
			return FText::FromString(LocalError);
		}
		if (Net && !Net->GetLastError().IsEmpty())
		{
			return FText::FromString(Net->GetLastError());
		}
		return Net && Net->IsSubmittingCharacter() ? FText::FromString(Net->GetStatus()) : FText::GetEmpty();
	});
	TSharedRef<STextBlock> ErrorText = Label(S, Error, 9.f, true, FLinearColor(1.f, 0.45f, 0.32f));
	ErrorText->SetAutoWrapText(true);

	ChildSlot.HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SMRPanel, InUI).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 3.f * Px)
			[
				Label(S, FText::FromString(Title), 15.f, true, Heading)
			]
			+ SVerticalBox::Slot().AutoHeight()[Tabs]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 0.f)
			[
				SNew(SMRPanel, InUI).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(5.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).WidthOverride(PageW * Px).HeightOverride(PageH * Px)[Pages]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ErrorText]
						+ SHorizontalBox::Slot().AutoWidth().Padding(3.f * Px, 0.f, 0.f, 0.f)
						[
							SNew(SMRTextButton, InUI).Text(LOCTEXT("Prev", "<< Prev")).TextSize(9.f)
								.IsEnabled_Lambda([this]() { return Tab != ETab::Name; })
								.OnClicked_Lambda([this]() { SetTab(static_cast<ETab>(FMath::Max(0, static_cast<int32>(Tab) - 1))); })
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.f * Px, 0.f, 0.f, 0.f)
						[
							SNew(SMRTextButton, InUI).Text(LOCTEXT("Next", "Next >>")).TextSize(9.f)
								.IsEnabled_Lambda([this]() { return Tab != ETab::Skills; })
								.OnClicked_Lambda([this]() { SetTab(static_cast<ETab>(FMath::Min(static_cast<int32>(ETab::Skills), static_cast<int32>(Tab) + 1))); })
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 4.f * Px, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SMRTextButton, InUI).Text(LOCTEXT("Ok", "OK")).TextSize(10.f).MinWidth(60.f)
						.IsEnabled_Lambda([this]() { const UMRNetSubsystem* Net = GetNet(); return Net && !Net->IsSubmittingCharacter() && Info().IsValid(); })
						.OnClicked(FSimpleDelegate::CreateSP(this, &SMRCharCreator::Ok))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(3.f * Px, 0.f, 0.f, 0.f)
				[
					SNew(SMRTextButton, InUI).Text(LOCTEXT("Cancel", "Cancel")).TextSize(10.f).MinWidth(60.f)
						.IsEnabled_Lambda([this]() { const UMRNetSubsystem* Net = GetNet(); return Net && !Net->IsSubmittingCharacter(); })
						.OnClicked(FSimpleDelegate::CreateSP(this, &SMRCharCreator::Cancel))
				]
			]
		]
	];
	if (Info().IsValid())
	{
		Begin();
	}
}

// ------------------------------------------------------------------------------ state

void SMRCharCreator::Begin(int32 Seed)
{
	const FMRCharInfo& I = Info();
	State = FMRNewCharacter();
	if (UMRNetSubsystem* Net = GetNet())
	{
		State.SlotId = Net->GetCreateSlot();
	}
	// the original starts with a random face and colours (charface.c)
	FRandomStream Rng(Seed >= 0 ? Seed : FMath::Rand());
	MRCharInfo::Randomize(State, I, Rng);
	MRCharInfo::ClampParts(State, I);
	LocalError.Reset();
	Tab = ETab::Name;
	if (NameField.IsValid())
	{
		NameField->SetText(FString());
	}
	if (DescBox.IsValid())
	{
		DescBox->SetText(FString());
	}
	Refresh();
	if (UI.IsValid())
	{
		UI->SetCreatorCapturing(true);
	}
}

void SMRCharCreator::Refresh()
{
	MRCharInfo::ClampParts(State, Info());
	RebuildAbilityLists();
	PushAppearance();
}

void SMRCharCreator::SetNameAndDescription(const FString& Name, const FString& Description)
{
	State.Name = Name;
	State.Description = Description;
	if (NameField.IsValid())
	{
		NameField->SetText(Name);
	}
	if (DescBox.IsValid())
	{
		DescBox->SetText(Description);
	}
}

void SMRCharCreator::FocusDescription()
{
	if (DescBox.IsValid())
	{
		DescBox->Focus();
	}
}

FString SMRCharCreator::GetDescriptionText() const
{
	return DescBox.IsValid() ? DescBox->GetText() : FString();
}

void SMRCharCreator::OnShown()
{
	if (Tab == ETab::Name && NameField.IsValid())
	{
		NameField->Focus();
	}
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

void SMRCharCreator::SetTab(ETab InTab)
{
	Tab = InTab;
	OnShown();
}

FMRSpriteAppearance SMRCharCreator::AppearanceOf(const FMRNewCharacter& C, const FMRCharInfo& I)
{
	FMRSpriteAppearance A;
	A.Look = C.bFemale ? FName(TEXT("player_female")) : FName(TEXT("player_male"));
	const FMRCharFaces& F = I.Faces(C.bFemale);
	auto Part = [](const TArray<FMRCharPart>& L, int32 Index) { return L.IsValidIndex(Index) ? FName(*L[Index].Bgf) : FName(); };
	A.HeadBgf = F.Head.Bgf.IsEmpty() ? FName() : FName(*F.Head.Bgf);
	A.HairBgf = Part(F.Hair, C.Hair);
	A.EyesBgf = Part(F.Eyes, C.Eyes);
	A.NoseBgf = Part(F.Noses, C.Nose);
	A.MouthBgf = Part(F.Mouths, C.Mouth);
	// the server's translations as the sprite colours: skin PT_BLUE_TO_SKIN1-4, hair by its xlat
	if (I.SkinXlats.IsValidIndex(C.Skin))
	{
		A.Skin = FMath::Clamp(I.SkinXlats[C.Skin] - 1, 0, FMRSpriteColours::NumSkins - 1);
	}
	if (I.HairXlats.IsValidIndex(C.HairColour))
	{
		for (int32 h = 0; h < FMRSpriteColours::NumHair; ++h)
		{
			if (FMRSpriteColours::HairXlat(h) == I.HairXlats[C.HairColour])
			{
				A.Hair = h;
				break;
			}
		}
	}
	return A;
}

void SMRCharCreator::PushAppearance()
{
	if (UI.IsValid() && Info().IsValid())
	{
		UI->SetCreatorAppearance(AppearanceOf(State, Info()));
	}
}

void SMRCharCreator::ApplyPreset(int32 Index)
{
	const TArray<TSharedPtr<FJsonValue>>* Presets;
	if (!Config->TryGetArrayField(TEXT("presets"), Presets) || !Presets->IsValidIndex(Index))
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>& V = (*Presets)[Index]->AsObject()->GetArrayField(TEXT("stats"));
	for (int32 i = 0; i < MRCharInfo::NumStats && i < V.Num(); ++i)
	{
		State.Stats[i] = FMath::Clamp(static_cast<int32>(V[i]->AsNumber()), MRCharInfo::StatMin, MRCharInfo::StatMax);
	}
	LocalError.Reset();
}

// ------------------------------------------------------------------------------ pages

TSharedRef<SWidget> SMRCharCreator::MakeNamePage()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	TSharedRef<STextBlock> Hint = Label(S, FText::FromString(Prompt(TEXT("name"))), InfoSize(8.f), false, Dim());
	Hint->SetAutoWrapText(true);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Hint]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 4.f * Px)
		[
			SAssignNew(NameField, SMRTextField, Ui).Width(130.f).MaxLength(MRCharInfo::NameMax)
			.OnChanged_Lambda([this]() { State.Name = NameField->GetText(); LocalError.Reset(); })
			.OnSubmit_Lambda([this]() { SetTab(ETab::Appearance); })
		]
		+ SVerticalBox::Slot().AutoHeight()[Label(S, FText::FromString(Prompt(TEXT("description"))), InfoSize(8.f), false, Dim())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 0.f)
		[
			SAssignNew(DescBox, SMRTextBox, Ui).Width(PageW - 14.f).Height(112.f).MaxLength(MRCharInfo::DescriptionMax)
			.OnChanged_Lambda([this]() { State.Description = DescBox->GetText(); })
		];
}

TSharedRef<SWidget> SMRCharCreator::MakePicker(const FText& Text, TFunction<void(int32)> Step, TFunction<FText()> Value)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Label(S, Text, InfoSize(9.f), false, Dim())]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SMRTextButton, Ui).Text(FText::FromString(TEXT("<"))).TextSize(9.f).MinWidth(16.f).OnClicked_Lambda([Step]() { Step(-1); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.f * Px, 0.f)
		[
			SNew(SBox).WidthOverride(34.f * Px).HAlign(HAlign_Center)[Label(S, TAttribute<FText>::CreateLambda(Value), 9.f)]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SMRTextButton, Ui).Text(FText::FromString(TEXT(">"))).TextSize(9.f).MinWidth(16.f).OnClicked_Lambda([Step]() { Step(1); })
		];
}

TSharedRef<SWidget> SMRCharCreator::MakeAppearancePage()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	auto Changed = [this]() { MRCharInfo::ClampParts(State, Info()); PushAppearance(); };
	auto Count = [](int32 Index, int32 N) { return FText::FromString(FString::Printf(TEXT("%d / %d"), Index + 1, N)); };
	const TSharedPtr<FJsonObject>* PartsJson = nullptr;
	Config->TryGetObjectField(TEXT("parts"), PartsJson);
	auto PartName = [PartsJson](const TCHAR* Key, const TCHAR* Default)
	{
		FString V = Default;
		if (PartsJson)
		{
			(*PartsJson)->TryGetStringField(Key, V);
		}
		return FText::FromString(V);
	};
	TSharedRef<STextBlock> Prompt0 = Label(S, FText::FromString(Prompt(TEXT("appearance"))), InfoSize(8.f), false, Dim());
	Prompt0->SetAutoWrapText(true);

	TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)[Prompt0]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SMRTextButton, Ui).Text(LOCTEXT("Male", "Male")).TextSize(9.f)
					.bActive_Lambda([this]() { return !State.bFemale; })
					.OnClicked_Lambda([this, Changed]() { State.bFemale = false; Changed(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f * Px, 0.f, 0.f, 0.f)
			[
				SNew(SMRTextButton, Ui).Text(LOCTEXT("Female", "Female")).TextSize(9.f)
					.bActive_Lambda([this]() { return State.bFemale; })
					.OnClicked_Lambda([this, Changed]() { State.bFemale = true; Changed(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Label(S, LOCTEXT("Skin", "Skin"), InfoSize(9.f), false, Dim())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMRSlider, Ui).Width(86.f).Height(7.f).Min(0).bTrack(true)
					.Max_Lambda([this]() { return FMath::Max(1, Info().SkinXlats.Num() - 1); })
					.Ticks_Lambda([this]() { return Info().SkinXlats.Num(); })
					.bShowValue(false)
					.Value_Lambda([this]() { return State.Skin; })
					.OnChanged_Lambda([this, Changed](int32 V) { State.Skin = V; Changed(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 3.f * Px)
		[
			SNew(SBox).WidthOverride(86.f * Px)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[Label(S, LOCTEXT("Light", "Light"), InfoSize(7.f), false, Dim())]
				+ SHorizontalBox::Slot().AutoWidth()[Label(S, LOCTEXT("Dark", "Dark"), InfoSize(7.f), false, Dim())]
			]
		];
	struct FPick { const TCHAR* Key; const TCHAR* Name; int32 FMRNewCharacter::* Field; TFunction<int32()> N; };
	const FPick Picks[] = {
		{TEXT("hair"), TEXT("Hair"), &FMRNewCharacter::Hair, [this]() { return Info().Faces(State.bFemale).Hair.Num(); }},
		{TEXT("hair_colour"), TEXT("Hair colour"), &FMRNewCharacter::HairColour, [this]() { return Info().HairXlats.Num(); }},
		{TEXT("eyes"), TEXT("Eyes"), &FMRNewCharacter::Eyes, [this]() { return Info().Faces(State.bFemale).Eyes.Num(); }},
		{TEXT("nose"), TEXT("Nose"), &FMRNewCharacter::Nose, [this]() { return Info().Faces(State.bFemale).Noses.Num(); }},
		{TEXT("mouth"), TEXT("Mouth"), &FMRNewCharacter::Mouth, [this]() { return Info().Faces(State.bFemale).Mouths.Num(); }},
	};
	for (const FPick& P : Picks)
	{
		int32 FMRNewCharacter::* Field = P.Field;
		TFunction<int32()> N = P.N;
		Controls->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.5f * Px)
		[
			MakePicker(PartName(P.Key, P.Name),
				[this, Field, N, Changed](int32 Dir) { State.*Field = State.*Field + Dir; Changed(); },
				[this, Field, N, Count]() { return Count(State.*Field, FMath::Max(1, N())); })
		];
	}

	return SNew(SHorizontalBox)
		// the face as the original creator showed it, large and turnable (drag it, or the arrows
		// under it: the head from every side)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Label(S, LOCTEXT("Face", "Face (drag to turn)"), InfoSize(8.f), false, Dim())]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SMRAvatar, Ui).Size(FVector2D(176.f, 176.f) * Px).Source(EMRAvatarSource::CreatorPortrait)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f * Px, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SMRTextButton, Ui).Text(FText::FromString(TEXT("<"))).TextSize(9.f).MinWidth(16.f)
						.OnClicked_Lambda([this]() { if (UI.IsValid()) UI->TurnCreatorPortrait(-1); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f * Px, 0.f)
				[
					Label(S, LOCTEXT("Turn", "Turn"), InfoSize(8.f), false, Dim())
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SMRTextButton, Ui).Text(FText::FromString(TEXT(">"))).TextSize(9.f).MinWidth(16.f)
						.OnClicked_Lambda([this]() { if (UI.IsValid()) UI->TurnCreatorPortrait(1); })
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f * Px, 0.f, 0.f, 0.f)[Controls];
}

TSharedRef<SWidget> SMRCharCreator::MakePointsBar(const FText& Text, TFunction<float()> Left, float Max)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f * Px, 0.f)[Label(S, Text, InfoSize(9.f), false, Dim())]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SMRBar, Ui).Width(110.f).Height(BarH).Color(S->Color(TEXT("graph_points"), FLinearColor(0.216f, 0.f, 0.f))).Max(Max)
				.Value_Lambda([Left]() { return FMath::Max(0.f, Left()); })
		];
}

TSharedRef<SWidget> SMRCharCreator::MakeStatsPage()
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const FString StatsPrompt = Prompt(TEXT("stats"));
	TSharedRef<STextBlock> Prompt0 = Label(S, FText::FromString(StatsPrompt), InfoSize(8.f), false, Dim());
	Prompt0->SetAutoWrapText(true);
	Prompt0->SetVisibility(StatsPrompt.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f * Px)[Prompt0];
	const TArray<TSharedPtr<FJsonValue>>* StatsJson = nullptr;
	Config->TryGetArrayField(TEXT("stats"), StatsJson);
	for (int32 i = 0; i < MRCharInfo::NumStats; ++i)
	{
		FString Name = StatKeys[i], Desc;
		if (StatsJson && StatsJson->IsValidIndex(i))
		{
			(*StatsJson)[i]->AsObject()->TryGetStringField(TEXT("name"), Name);
			(*StatsJson)[i]->AsObject()->TryGetStringField(TEXT("desc"), Desc);
		}
		TSharedRef<STextBlock> DescText = Label(S, FText::FromString(Desc), 8.5f, false, Dim());
		DescText->SetAutoWrapText(true);
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.5f * Px)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(46.f * Px)[Label(S, FText::FromString(Name), 9.f)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMRSlider, Ui).Width(104.f).Height(BarH).Min(0).Max(MRCharInfo::StatMax)
					.Value_Lambda([this, i]() { return State.Stats[i]; })
					.OnChanged_Lambda([this, i](int32 V)
					{
						// within the range, and no more than the points left (as the original's sliders)
						const int32 Room = MRCharInfo::StatPointsLeft(State) + State.Stats[i];
						State.Stats[i] = FMath::Clamp(V, MRCharInfo::StatMin, FMath::Min(MRCharInfo::StatMax, Room));
						LocalError.Reset();
					})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4.f * Px, 0.f, 0.f, 0.f)[DescText]
		];
	}
	TSharedRef<SVerticalBox> Presets = SNew(SVerticalBox);
	const TArray<TSharedPtr<FJsonValue>>* PresetsJson = nullptr;
	if (Config->TryGetArrayField(TEXT("presets"), PresetsJson))
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 p = 0; p < PresetsJson->Num(); ++p)
		{
			const TSharedPtr<FJsonObject> O = (*PresetsJson)[p]->AsObject();
			Row->AddSlot().FillWidth(1.f).Padding(p > 0 ? 2.f * Px : 0.f, 0.f, 0.f, 0.f)
			[
				SNew(SMRTextButton, Ui).Text(FText::FromString(O->GetStringField(TEXT("name")))).TextSize(8.f)
					.ToolTipText(FText::FromString(O->GetStringField(TEXT("desc"))))
					.OnClicked_Lambda([this, p]() { ApplyPreset(p); })
			];
		}
		Presets->AddSlot().AutoHeight()[Label(S, LOCTEXT("Suggestions", "Suggestions for newcomers"), InfoSize(8.f), false, Dim())];
		Presets->AddSlot().AutoHeight().Padding(0.f, 1.f * Px, 0.f, 0.f)[Row];
	}
	Box->AddSlot().AutoHeight().Padding(0.f, 3.f * Px, 0.f, 10.f * Px)[Presets];
	Box->AddSlot().AutoHeight()
	[
		MakePointsBar(LOCTEXT("StatPoints", "Stat points left"),
			[this]() { return static_cast<float>(MRCharInfo::StatPointsLeft(State)); }, MRCharInfo::StatTotal - MRCharInfo::NumStats * MRCharInfo::StatStart)
	];
	return Box;
}

FText SMRCharCreator::AbilityLabel(const FMRCharAbility& A, bool bSkill) const
{
	// "Faren 1: fog" (char.c: the level from the cost)
	return FText::FromString(FString::Printf(TEXT("%s %d: %s"), *UMRGameDataSubsystem::SchoolName(A.School).ToString(), A.Level(), *A.Name));
}

FString SMRCharCreator::CantAdd(bool bSkill, int32 InfoIndex) const
{
	const TArray<FMRCharAbility>& List = bSkill ? Info().Skills : Info().Spells;
	if (!List.IsValidIndex(InfoIndex))
	{
		return TEXT("-");
	}
	const FMRCharAbility& A = List[InfoIndex];
	if (A.Cost > MRCharInfo::AbilityPointsLeft(State, Info()))
	{
		return TEXT("Not enough points left.");
	}
	if (!bSkill && (A.School == MRCharInfo::SchoolShalille || A.School == MRCharInfo::SchoolQor))
	{
		const uint8 Other = A.School == MRCharInfo::SchoolShalille ? MRCharInfo::SchoolQor : MRCharInfo::SchoolShalille;
		for (uint32 Num : State.Spells)
		{
			const FMRCharAbility* Have = Info().Spells.FindByPredicate([Num](const FMRCharAbility& X) { return X.Num == Num; });
			if (Have && Have->School == Other)
			{
				return Prompt(TEXT("spells_rule"));
			}
		}
	}
	return FString();
}

void SMRCharCreator::RebuildAbilityLists()
{
	for (int32 k = 0; k < 2; ++k)
	{
		const bool bSkill = k == 1;
		const TArray<FMRCharAbility>& List = bSkill ? Info().Skills : Info().Spells;
		const TArray<uint32>& Have = bSkill ? State.Skills : State.Spells;
		AvailableRows[k].Reset();
		ChosenRows[k].Reset();
		for (int32 i = 0; i < List.Num(); ++i)
		{
			(Have.Contains(List[i].Num) ? ChosenRows[k] : AvailableRows[k]).Add(i);
		}
		// sorted by their labels, as the original's list boxes (LBS_SORT)
		auto ByLabel = [this, &List, bSkill](int32 A, int32 B)
		{
			return AbilityLabel(List[A], bSkill).ToString().Compare(AbilityLabel(List[B], bSkill).ToString(), ESearchCase::IgnoreCase) < 0;
		};
		AvailableRows[k].Sort(ByLabel);
		ChosenRows[k].Sort(ByLabel);
		TArray<FText> AItems, CItems;
		TArray<bool> AEnabled;
		for (int32 i : AvailableRows[k])
		{
			AItems.Add(AbilityLabel(List[i], bSkill));
			AEnabled.Add(CantAdd(bSkill, i).IsEmpty());
		}
		for (int32 i : ChosenRows[k])
		{
			CItems.Add(AbilityLabel(List[i], bSkill));
		}
		if (Available[k].IsValid())
		{
			Available[k]->SetItems(AItems, AEnabled);
		}
		if (Chosen[k].IsValid())
		{
			Chosen[k]->SetItems(CItems);
		}
	}
}

void SMRCharCreator::AddAbility(bool bSkill, int32 InfoIndex)
{
	const TArray<FMRCharAbility>& List = bSkill ? Info().Skills : Info().Spells;
	TArray<uint32>& Have = bSkill ? State.Skills : State.Spells;
	if (!List.IsValidIndex(InfoIndex) || Have.Contains(List[InfoIndex].Num))
	{
		return;
	}
	const FString Why = CantAdd(bSkill, InfoIndex);
	if (!Why.IsEmpty())
	{
		LocalError = Why;
		return;
	}
	LocalError.Reset();
	Have.Add(List[InfoIndex].Num);
	RebuildAbilityLists();
}

void SMRCharCreator::AddSelected(bool bSkill)
{
	const int32 k = bSkill ? 1 : 0;
	const int32 Row = Available[k].IsValid() ? Available[k]->GetSelected() : -1;
	if (AvailableRows[k].IsValidIndex(Row))
	{
		AddAbility(bSkill, AvailableRows[k][Row]);
	}
}

void SMRCharCreator::RemoveSelected(bool bSkill)
{
	const int32 k = bSkill ? 1 : 0;
	const int32 Row = Chosen[k].IsValid() ? Chosen[k]->GetSelected() : -1;
	if (!ChosenRows[k].IsValidIndex(Row))
	{
		return;
	}
	const TArray<FMRCharAbility>& List = bSkill ? Info().Skills : Info().Spells;
	(bSkill ? State.Skills : State.Spells).Remove(List[ChosenRows[k][Row]].Num);
	LocalError.Reset();
	RebuildAbilityLists();
}

FText SMRCharCreator::SelectedInfo(bool bSkill) const
{
	const int32 k = bSkill ? 1 : 0;
	const TArray<FMRCharAbility>& List = bSkill ? Info().Skills : Info().Spells;
	const TSharedPtr<SMRSelectList>& L = bInfoFromChosen[k] ? Chosen[k] : Available[k];
	const TArray<int32>& Rows = bInfoFromChosen[k] ? ChosenRows[k] : AvailableRows[k];
	const int32 Row = L.IsValid() ? L->GetSelected() : -1;
	if (!Rows.IsValidIndex(Row) || !List.IsValidIndex(Rows[Row]))
	{
		return FText::GetEmpty();
	}
	const FMRCharAbility& A = List[Rows[Row]];
	return FText::FromString(FString::Printf(TEXT("%s Lv. %d: %s\n\nCost: %d"),
		*UMRGameDataSubsystem::SchoolName(A.School).ToString(), A.Level(), *A.Desc, A.Cost));
}

TSharedRef<SWidget> SMRCharCreator::MakeAbilityPage(bool bSkills)
{
	UMRUISubsystem* Ui = UI.Get();
	UMRUIStyle* S = Ui->GetStyle();
	const float Px = S->Px();
	const int32 k = bSkills ? 1 : 0;
	TSharedRef<STextBlock> Prompt0 = Label(S, FText::FromString(Prompt(bSkills ? TEXT("skills") : TEXT("spells"))), InfoSize(8.f), false, Dim());
	TSharedRef<STextBlock> InfoText = Label(S, TAttribute<FText>::CreateLambda([this, bSkills]() { return SelectedInfo(bSkills); }), 7.f, false, Dim());
	InfoText->SetAutoWrapText(true);
	TSharedRef<STextBlock> Rule = Label(S, FText::FromString(bSkills ? FString() : Prompt(TEXT("spells_rule"))), InfoSize(7.f), false, Dim());
	Rule->SetAutoWrapText(true);
	Rule->SetVisibility(bSkills ? EVisibility::Collapsed : EVisibility::Visible);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f * Px)[Prompt0]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(S, bSkills ? LOCTEXT("SkillsAvail", "Available skills") : LOCTEXT("SpellsAvail", "Available spells"), InfoSize(8.f), false, Dim())]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(Available[k], SMRSelectList, Ui).Width(ListW).Height(ListH).TextSize(InfoSize(8.f))
						.OnSelected_Lambda([this, k](int32) { bInfoFromChosen[k] = false; })
						.OnActivated_Lambda([this, bSkills](int32) { AddSelected(bSkills); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(ListW * Px)[Rule]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f * Px, 9.f * Px, 4.f * Px, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SMRTextButton, Ui).Text(bSkills ? LOCTEXT("AddSkill", "Add skill >>") : LOCTEXT("AddSpell", "Add spell >>")).TextSize(8.f)
						.OnClicked_Lambda([this, bSkills]() { AddSelected(bSkills); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f * Px, 0.f, 4.f * Px)
				[
					SNew(SMRTextButton, Ui).Text(bSkills ? LOCTEXT("RemoveSkill", "<< Remove skill") : LOCTEXT("RemoveSpell", "<< Remove spell")).TextSize(8.f)
						.OnClicked_Lambda([this, bSkills]() { RemoveSelected(bSkills); })
				]
				+ SVerticalBox::Slot().FillHeight(1.f)[InfoText]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(S, bSkills ? LOCTEXT("SkillsHave", "Skills you have") : LOCTEXT("SpellsHave", "Spells you have"), InfoSize(8.f), false, Dim())]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(Chosen[k], SMRSelectList, Ui).Width(ListW - 16.f).Height(ListH).TextSize(InfoSize(8.f))
						.OnSelected_Lambda([this, k](int32) { bInfoFromChosen[k] = true; })
						.OnActivated_Lambda([this, bSkills](int32) { RemoveSelected(bSkills); })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 13.f * Px, 0.f, 0.f)
		[
			MakePointsBar(LOCTEXT("AbilityPoints", "Spell and skill points left"),
				[this]() { return static_cast<float>(MRCharInfo::AbilityPointsLeft(State, Info())); }, MRCharInfo::AbilityPoints)
		];
}

// ------------------------------------------------------------------------------ actions

void SMRCharCreator::Ok()
{
	UMRNetSubsystem* Net = GetNet();
	if (!Net || !Info().IsValid())
	{
		return;
	}
	State.Name = NameField.IsValid() ? NameField->GetText().TrimStartAndEnd() : State.Name;
	State.Description = DescBox.IsValid() ? DescBox->GetText() : State.Description;
	FString Why;
	switch (MRCharInfo::Validate(State, Info(), Why))
	{
	case MRCharInfo::EProblem::None:
		if (UnspentPoints(Why))
		{
			break;  // (UnspentPoints showed the page)
		}
		LocalError.Reset();
		Net->CreateCharacter(State);
		return;
	case MRCharInfo::EProblem::Name:
	case MRCharInfo::EProblem::Description:
		SetTab(ETab::Name);
		break;
	case MRCharInfo::EProblem::Stats:
		SetTab(ETab::Stats);
		break;
	case MRCharInfo::EProblem::Abilities:
		SetTab(ETab::Spells);
		break;
	}
	LocalError = Why;
}

bool SMRCharCreator::UnspentPoints(FString& OutMessage)
{
	// every stat point (they can always all be spent: six stats of 50 is more than 220)
	const int32 StatsLeft = MRCharInfo::StatPointsLeft(State);
	if (StatsLeft > 0)
	{
		OutMessage = FString::Printf(TEXT("Spend all of your points first: %d stat point%s left."), StatsLeft, StatsLeft == 1 ? TEXT("") : TEXT("s"));
		SetTab(ETab::Stats);
		return true;
	}
	// the spell and skill points, as far as they can be spent: costs are 10 and 25, so a few may
	// be left with nothing that fits (four 10-point picks leave 5)
	const int32 AbilitiesLeft = MRCharInfo::AbilityPointsLeft(State, Info());
	for (int32 k = 0; k < 2 && AbilitiesLeft > 0; ++k)
	{
		const bool bSkill = k == 1;
		const TArray<FMRCharAbility>& List = bSkill ? Info().Skills : Info().Spells;
		const TArray<uint32>& Have = bSkill ? State.Skills : State.Spells;
		for (int32 i = 0; i < List.Num(); ++i)
		{
			if (!Have.Contains(List[i].Num) && CantAdd(bSkill, i).IsEmpty())
			{
				OutMessage = FString::Printf(TEXT("Spend all of your points first: %d spell and skill points left."), AbilitiesLeft);
				SetTab(State.Spells.Num() == 0 && State.Skills.Num() > 0 ? ETab::Skills : ETab::Spells);
				return true;
			}
		}
	}
	return false;
}

void SMRCharCreator::Cancel()
{
	LocalError.Reset();
	if (UI.IsValid())
	{
		UI->SetCreatorCapturing(false);
	}
	if (UMRNetSubsystem* Net = GetNet())
	{
		Net->CancelCreation();
	}
}

FReply SMRCharCreator::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE

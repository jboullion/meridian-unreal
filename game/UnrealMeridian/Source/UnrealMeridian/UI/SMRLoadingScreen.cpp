#include "UI/SMRLoadingScreen.h"

#include "Core/MRWarmup.h"
#include "GeneralProjectSettings.h"
#include "Rendering/DrawElements.h"
#include "UI/MRUIStyle.h"
#include "UI/MRUISubsystem.h"
#include "UI/SMRHUD.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MRLoading"

namespace
{
	constexpr float WidthPx = 210.f;  // the window's inside, original pixels (the login screen's)

	FLinearColor Dim() { return FLinearColor(0.78f, 0.75f, 0.68f); }

	/** The warm-up's progress: the HUD bars' look (dark trough, gold fill, a highlight). */
	class SMRProgress : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMRProgress) {}
			SLATE_ATTRIBUTE(float, Value)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UMRUIStyle* InStyle)
		{
			Style = InStyle;
			Value = InArgs._Value;
		}
		virtual FVector2D ComputeDesiredSize(float) const override
		{
			const float Px = Style.IsValid() ? Style->Px() : 2.f;
			return FVector2D((WidthPx - 10.f) * Px, 5.f * Px);
		}
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& WStyle, bool bParentEnabled) const override
		{
			if (!Style.IsValid())
			{
				return Layer;
			}
			const FVector2f Size(Geo.GetLocalSize());
			const float V = FMath::Clamp(Value.Get(), 0.f, 1.f);
			const FSlateBrush* W = Style->White();
			MRPaint::Box(Out, Layer, Geo, W, FVector2f::ZeroVector, Size, Style->Color(TEXT("bar_empty"), FLinearColor(0.02f, 0.02f, 0.02f, 0.85f)));
			MRPaint::Box(Out, Layer + 1, Geo, W, FVector2f::ZeroVector, FVector2f(Size.X * V, Size.Y),
				Style->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)));
			MRPaint::Box(Out, Layer + 2, Geo, W, FVector2f::ZeroVector, FVector2f(Size.X * V, Size.Y * 0.3f), FLinearColor(1.f, 1.f, 1.f, 0.18f));
			return Layer + 2;
		}

	private:
		TWeakObjectPtr<UMRUIStyle> Style;
		TAttribute<float> Value;
	};
}

void SMRLoadingScreen::Construct(const FArguments& InArgs, UMRUISubsystem* InUI, UMRWarmup* InWarmup)
{
	UI = InUI;
	Warmup = InWarmup;
	UMRUIStyle* S = InUI->GetStyle();
	const float Px = S->Px();
	using MRUI::Label;

	auto Status = TAttribute<FText>::CreateLambda([this]()
	{
		const UMRWarmup* W = Warmup.Get();
		return W ? W->GetStatus() : FText::GetEmpty();
	});
	auto Value = TAttribute<float>::CreateLambda([this]()
	{
		const UMRWarmup* W = Warmup.Get();
		return W ? W->GetProgress() : 0.f;
	});
	TSharedRef<STextBlock> Hint = Label(S, LOCTEXT("Hint",
		"The first start after an update prepares the shaders; later starts are quicker."), 8.f, false, Dim());
	Hint->SetAutoWrapText(true);
	Hint->SetJustification(ETextJustify::Center);

	ChildSlot
	[
		SNew(SOverlay)
		// opaque: the world behind is still popping in
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(S->White()).ColorAndOpacity(FLinearColor(0.01f, 0.01f, 0.012f, 1.f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SMRPanel, InUI).Background(TEXT("bkgnd")).Frame(TEXT("edge")).bCorners(true).Padding(6.f)
			[
				SNew(SBox).WidthOverride(WidthPx * Px)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 4.f * Px)
					[
						Label(S, LOCTEXT("Title", "Unreal Meridian"), 15.f, true, S->Color(TEXT("heading"), FLinearColor(1.f, 0.75f, 0.3f)))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SMRPanel, InUI).Background(TEXT("invbkgnd")).Frame(TEXT("inset")).Padding(5.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f * Px, 0.f, 0.f)
							[
								Label(S, Status, 11.f, true)
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 5.f * Px)
							[
								SNew(SMRProgress, S).Value(Value)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(2.f * Px, 0.f, 2.f * Px, 3.f * Px)
							[
								Hint
							]
						]
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(8.f * Px, 4.f * Px)
		[
			Label(S, FText::FromString(TEXT("v") + GetDefault<UGeneralProjectSettings>()->ProjectVersion), 8.f, false, Dim())
		]
	];
}

#undef LOCTEXT_NAMESPACE

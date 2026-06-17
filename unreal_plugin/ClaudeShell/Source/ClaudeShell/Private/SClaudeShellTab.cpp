// Copyright Nexus Team. All Rights Reserved.

#include "SClaudeShellTab.h"
#include "SWebBrowser.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SThrobber.h"

DEFINE_LOG_CATEGORY_STATIC(LogClaudeShellTab, Log, All);

// Slot indices for the widget switcher
static constexpr int32 SLOT_LOADING  = 0;
static constexpr int32 SLOT_TERMINAL = 1;
static constexpr int32 SLOT_ERROR    = 2;

void SClaudeShellTab::Construct(const FArguments& InArgs)
{
	RelayPort = InArgs._RelayPort;
	SessionId = InArgs._SessionId;
	Token = InArgs._Token;

	FString Url = BuildTerminalUrl();

	// ── Build web browser (Slot 1: Terminal) ──
	SAssignNew(WebBrowser, SWebBrowser)
		.InitialURL(Url)
		.ShowControls(false)
		.ShowAddressBar(false)
		.ShowErrorMessage(false)  // We handle errors via the switcher
		.SupportsTransparency(false)
		.BrowserFrameRate(60);

	// ── Build the 3-slot widget switcher ──
	ChildSlot
	[
		SAssignNew(Switcher, SWidgetSwitcher)
		.WidgetIndex(SLOT_LOADING)  // Start on loading

		// ── Slot 0: Loading ──
		+ SWidgetSwitcher::Slot()
		[
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0, 0, 0, 8)
				[
					SNew(SCircularThrobber)
					.Radius(16.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Starting Claude Shell...")))
				]
			]
		]

		// ── Slot 1: Terminal (Web Browser) ──
		+ SWidgetSwitcher::Slot()
		[
			WebBrowser.ToSharedRef()
		]

		// ── Slot 2: Error ──
		+ SWidgetSwitcher::Slot()
		[
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0, 0, 0, 16)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Claude Shell failed to load.\n\nThe relay may have crashed or timed out.")))
					.Justification(ETextJustify::Center)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0, 0, 0, 8)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Retry")))
					.OnClicked(FOnClicked::CreateSP(this, &SClaudeShellTab::OnRetryClicked))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("View Log")))
					.OnClicked(FOnClicked::CreateSP(this, &SClaudeShellTab::OnViewLogClicked))
				]
			]
		]
	];

	// ── Start load timeout timer ──
	LoadStartTime = FPlatformTime::Seconds();
	bLoaded = false;

	TimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SClaudeShellTab::OnLoadTimeout),
		1.0f  // Check every second
	);

	// ── Listen for browser load completion ──
	// SWebBrowser doesn't expose a delegate directly in all engine versions,
	// so we use the timeout ticker to detect when the page is loaded by
	// polling. When loaded, the browser should have a non-empty title.
	// We switch from Loading to Terminal after a brief delay to let the page render.

	UE_LOG(LogClaudeShellTab, Log, TEXT("Tab created -> %s (session=%s)"), *Url, *SessionId);
}

SClaudeShellTab::~SClaudeShellTab()
{
	if (TimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TimeoutTickerHandle);
		TimeoutTickerHandle.Reset();
	}
}

void SClaudeShellTab::ShowSlot(int32 Index)
{
	if (Switcher.IsValid())
	{
		Switcher->SetActiveWidgetIndex(Index);
	}
}

bool SClaudeShellTab::OnLoadTimeout(float DeltaTime)
{
	if (bLoaded)
	{
		// Already transitioned — stop ticking
		return false;
	}

	double Elapsed = FPlatformTime::Seconds() - LoadStartTime;

	// Check if browser seems to have loaded (it renders content).
	// After 2 seconds, switch to terminal view — the page should be loading.
	// The web browser widget doesn't provide a reliable "page loaded" callback,
	// so we trust a small delay for the HTTP round-trip to localhost.
	if (Elapsed > 2.0)
	{
		bLoaded = true;
		ShowSlot(SLOT_TERMINAL);
		UE_LOG(LogClaudeShellTab, Log, TEXT("Switched to terminal view (%.1fs)"), Elapsed);

		// Focus the browser
		if (WebBrowser.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(WebBrowser, EFocusCause::SetDirectly);
		}

		return false;  // Stop ticking
	}

	// Check for timeout
	if (Elapsed > LOAD_TIMEOUT_SEC)
	{
		bLoaded = true;  // Prevent further checks
		ShowSlot(SLOT_ERROR);
		UE_LOG(LogClaudeShellTab, Error,
			TEXT("Terminal load timed out after %.0fs"), LOAD_TIMEOUT_SEC);
		return false;  // Stop ticking
	}

	return true;  // Keep ticking
}

void SClaudeShellTab::OnBrowserLoadCompleted()
{
	if (!bLoaded)
	{
		bLoaded = true;
		ShowSlot(SLOT_TERMINAL);

		if (WebBrowser.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(WebBrowser, EFocusCause::SetDirectly);
		}
	}
}

FReply SClaudeShellTab::OnRetryClicked()
{
	// Reload the browser to retry connection
	bLoaded = false;
	LoadStartTime = FPlatformTime::Seconds();
	ShowSlot(SLOT_LOADING);

	if (WebBrowser.IsValid())
	{
		FString Url = BuildTerminalUrl();
		WebBrowser->LoadURL(Url);
	}

	// Restart the timeout ticker
	if (!TimeoutTickerHandle.IsValid())
	{
		TimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &SClaudeShellTab::OnLoadTimeout),
			1.0f
		);
	}

	UE_LOG(LogClaudeShellTab, Log, TEXT("Retrying terminal load..."));
	return FReply::Handled();
}

FReply SClaudeShellTab::OnViewLogClicked()
{
	// Open the Output Log tab
	FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog"));
	return FReply::Handled();
}

FString SClaudeShellTab::BuildTerminalUrl() const
{
	// terminal.html expects ?session=X&token=Y as query params
	return FString::Printf(
		TEXT("http://127.0.0.1:%d/terminal.html?session=%s&token=%s"),
		RelayPort, *SessionId, *Token);
}

// --------------------------------------------------------------------
// Focus forwarding — ensure keyboard input reaches xterm.js
// --------------------------------------------------------------------

FReply SClaudeShellTab::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (WebBrowser.IsValid())
	{
		return FReply::Handled().SetUserFocus(WebBrowser.ToSharedRef(), InFocusEvent.GetCause());
	}
	return FReply::Handled();
}

void SClaudeShellTab::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (WebBrowser.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(WebBrowser, EFocusCause::Mouse);
	}
}

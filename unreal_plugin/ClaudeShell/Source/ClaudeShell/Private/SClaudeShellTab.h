// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWebBrowser;
class SWidgetSwitcher;

/**
 * Claude Shell tab widget — web terminal with loading and error states.
 *
 * Uses SWidgetSwitcher with three slots:
 *   Slot 0: Loading spinner (shown during relay startup)
 *   Slot 1: Terminal (SWebBrowser loading terminal.html)
 *   Slot 2: Error panel (shown on timeout or load failure, with Retry + View Log)
 *
 * The tab receives session_id and token from ClaudeShellModule and passes
 * them to the frontend via URL query parameters.
 */
class SClaudeShellTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SClaudeShellTab)
		: _RelayPort(19220)
		, _SessionId()
		, _Token()
	{}
		SLATE_ARGUMENT(int32, RelayPort)
		SLATE_ARGUMENT(FString, SessionId)
		SLATE_ARGUMENT(FString, Token)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SClaudeShellTab() override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	/** Switch to a specific widget slot (0=Loading, 1=Terminal, 2=Error). */
	void ShowSlot(int32 Index);

	/** Called when the web browser finishes loading a URL. */
	void OnBrowserLoadCompleted();

	/** Called when the load timeout fires. */
	bool OnLoadTimeout(float DeltaTime);

	/** Retry button handler — reload the browser. */
	FReply OnRetryClicked();

	/** View Log button handler — open the Output Log. */
	FReply OnViewLogClicked();

	/** Build the terminal URL with session and token query params. */
	FString BuildTerminalUrl() const;

	// ── Widget references ──
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SWebBrowser> WebBrowser;

	// ── Config ──
	int32 RelayPort = 19220;
	FString SessionId;
	FString Token;

	// ── Timeout state ──
	FTSTicker::FDelegateHandle TimeoutTickerHandle;
	double LoadStartTime = 0.0;
	static constexpr double LOAD_TIMEOUT_SEC = 30.0;
	bool bLoaded = false;
};

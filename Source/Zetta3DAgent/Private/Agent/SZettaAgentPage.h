// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaAgentAttachments.h"
#include "ZettaAgentMentions.h"
#include "ZettaAgentRuntime.h"

class SBox;
class SMultiLineEditableTextBox;
class SScrollBox;
class SVerticalBox;
class SWidgetSwitcher;
class SWrapBox;
struct FSlateBrush;

/**
 * The Agent page of the Zetta 3D window: the conversation, the message box with its tags and attachments, the
 * model, thinking and autonomy menus under it, History, Memory, and the settings for what the agent runs on.
 */
class SZettaAgentPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaAgentPage) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SZettaAgentPage() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/**
	 * Tags these in the message box ("Add to Zetta Agent", the shortcut), opening the Agent page if it is not open.
	 * Returns how many could be tagged.
	 */
	static int32 AddToChat(const TArray<FZettaMention>& Mentions);
	/** Opens the Agent page on its settings (what the agent runs on, the user's own keys). */
	static void ShowSettings();

private:
	// Layout
	TSharedRef<SWidget> MakeChat();
	TSharedRef<SWidget> MakeToolbar();
	TSharedRef<SWidget> MakeComposer();
	TSharedRef<SWidget> MakePickers();
	TSharedRef<SWidget> MakeMentionPicker();
	TSharedRef<SWidget> MakeWorking();
	TSharedRef<SWidget> MoreMenu();
	TSharedRef<SWidget> AttachMenu();
	TSharedRef<SWidget> Welcome();
	void SetSettingsVisible(bool bVisible);

	// Transcript
	void MarkDirty() { bDirty = true; }
	void RenderTranscript();
	TSharedRef<SWidget> RenderItem(const FZettaChatItemPtr& Item);
	TSharedRef<SWidget> StepRow(const FZettaChatItemPtr& Item);
	TSharedRef<SWidget> ErrorCard(const FZettaChatItemPtr& Item);
	TSharedRef<SWidget> ImageCard(const FZettaChatItemPtr& Item);
	void SaveImage(const FZettaChatItemPtr& Item);

	// Composer
	FReply OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& Key);
	void Submit(bool bFromButton);
	bool CanSee() const;
	void AddImage(const FZettaAgentFile& File, const FString& Label, const TArray<uint8>& Original, const FString& Name);
	void AddFiles(const TArray<FString>& Paths);
	void AddMention(const FZettaMention& Mention);
	void AttachViewport();
	bool Fits(int32 Extra);
	void RenderAttachments();
	void TakePending();

	// @ mentions
	void UpdateMention();
	void RefreshMention();
	void CloseMention();
	void PickMention(int32 Index);
	void SetMentionMode(const FString& Mode);

	// History and memory
	enum class EPanel : uint8 { None, History, Memory };
	void TogglePanel(EPanel Which);
	void LoadHistory();
	void RenderHistory();
	void DeleteSession(int64 Id, const FString& Title);
	void LoadMemory();
	void RenderMemory();

	// Transcript state
	struct FRow
	{
		FZettaChatItemPtr Item;
		uint32 Signature = 0;
		TSharedPtr<SBox> Box;
	};
	TArray<FRow> Rows;
	/** The chat's text size (points from the standard): restyles the transcript and the message box. */
	void SetTextSize(int32 Step);
	/** Cmd/Ctrl + = / − / 0 anywhere in the chat: larger, smaller, standard. */
	bool HandleTextSizeKey(const FKeyEvent& Event);
	TSharedPtr<SScrollBox> Scroll;
	TSharedPtr<SVerticalBox> List;
	TSharedPtr<SWidget> WelcomeWidget;
	bool bDirty = true;
	int32 LastCount = 0;
	uint32 LastSignature = 0;

	TSharedPtr<SWidgetSwitcher> Main;

	// Composer state
	TSharedPtr<SMultiLineEditableTextBox> Input;
	TSharedPtr<SWrapBox> AttachmentsBox;
	TArray<FZettaAgentFile> Images;
	TArray<FString> ImageLabels;
	/** Each attached picture's "attached:<name>", so the picture tools can edit or combine it. */
	TArray<FString> ImageKeys;
	TArray<TSharedPtr<FSlateBrush>> ImageBrushes;
	TArray<FZettaAttachment> Files;
	TArray<FZettaMention> Mentions;
	bool bDragOver = false;

	// @ picker state
	bool bMentionOpen = false;
	bool bSuppressMention = false;
	FString MentionMode;
	FString MentionQuery;
	int32 MentionLine = 0;
	int32 MentionAt = 0;
	int32 MentionCursor = 0;
	int32 MentionSelected = 0;
	TArray<FZettaMention> MentionResults;
	TSharedPtr<SVerticalBox> MentionList;
	TSharedPtr<SScrollBox> MentionScroll;

	// Panels
	EPanel Panel = EPanel::None;
	TSharedPtr<SBox> HistoryBody;
	TSharedPtr<SVerticalBox> HistoryList;
	TArray<TSharedPtr<FJsonObject>> Sessions;
	FString HistoryFilter;
	TSharedPtr<SBox> MemoryBody;
	TSharedPtr<FJsonObject> MemoryData;
	int64 EditingMemory = 0;

	FDelegateHandle RuntimeHandle, AuthHandle, CatalogHandle;

	static TArray<FZettaMention> PendingMentions;
	static bool bPendingSettings;
};

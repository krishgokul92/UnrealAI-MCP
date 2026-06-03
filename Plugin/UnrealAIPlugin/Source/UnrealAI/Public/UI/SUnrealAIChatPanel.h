// UnrealAI - Chat Panel Widget
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Interfaces/IHttpRequest.h"

/** Role of a message in the chat history. */
enum class EChatMessageRole : uint8
{
    User,
    Assistant,
    System
};

/** Single chat message. */
struct FChatMessage
{
    EChatMessageRole Role;
    FString Content;

    FChatMessage(EChatMessageRole InRole, const FString& InContent)
        : Role(InRole), Content(InContent) {}
};

/** A parsed segment of a chat message — either prose or a fenced code block. */
struct FChatContentSegment
{
    FString Text;
    FString Language; // empty for prose; e.g. "t3d", "json", "cpp" for code
    bool bIsCode = false;
};

/**
 * Main chat panel widget for UnrealAI.
 * Phase 2: UI shell with placeholder responses.
 * Phase 3: Wired to Python agent via HTTP.
 */
class UNREALAI_API SUnrealAIChatPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SUnrealAIChatPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    /** Send the user's message. Phase 2: echo placeholder. */
    FReply OnSendClicked();

    /** Handle Enter key in input box to send. */
    void OnInputTextCommitted(const FText& InText, ETextCommit::Type CommitType);

    /** Called when the model dropdown selection changes. */
    void OnModelChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

    /** Called when Clear button is clicked. */
    FReply OnClearClicked();

    /** Called when Save button is clicked. */
    FReply OnSaveClicked();

    /** Called when Load button is clicked. */
    FReply OnLoadClicked();

    /** Called when 'Open in VS Code' is clicked. Launches VS Code against the workspace so the user can talk to Copilot via the MCP server. */
    FReply OnOpenInVSCodeClicked();

    /** Load a specific saved session by filename. */
    void LoadSession(const FString& Filename);

    /** Generate a widget for a single entry in the model combo box. */
    TSharedRef<SWidget> MakeModelComboWidget(TSharedPtr<FString> InItem);

    /** Add a message to history and refresh the display. */
    void AddMessage(EChatMessageRole Role, const FString& Content);

    /** Rebuild the scrollable message list from history. */
    void RefreshMessageList();

    /** Create a widget for a single message. */
    TSharedRef<SWidget> CreateMessageWidget(const FChatMessage& Message);

    /** Split a message body into prose / code-block segments. */
    static TArray<FChatContentSegment> SplitMessageIntoSegments(const FString& Content);

    /** Build a wrapped prose text widget. */
    TSharedRef<SWidget> CreateProseWidget(const FString& Text);

    /** Build a scrollable monospace code-block widget with a Copy button. */
    TSharedRef<SWidget> CreateCodeBlockWidget(const FString& CodeText, const FString& Language);

    /** Scroll to the bottom of the message list. */
    void ScrollToBottom();

    /** Send message to the Python agent over HTTP. */
    void SendMessageToAgent(const FString& UserMessage);

    /** Callback for the /chat HTTP response. */
    void OnChatResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);

    /** Poll the agent for a job's status. */
    void PollJobStatus(const FString& JobId);

    /** Callback for the /chat/status HTTP response. */
    void OnJobStatusReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, FString JobId);

    /** Fetch the list of models from the agent's /models endpoint. */
    void FetchAvailableModels();

    /** Callback for the /models HTTP response. */
    void OnModelsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);

    /** Update the status text (thread-safe). */
    void SetStatus(const FString& Text, const FLinearColor& Color);

    /** Set UI enabled/disabled state while a request is in flight. */
    void SetBusy(bool bBusy);

    /** Gather a snapshot of the current UE project context (name, map, selection, etc.). */
    TSharedPtr<FJsonObject> GatherProjectContext() const;

private:
    /** All chat messages in order. */
    TArray<FChatMessage> ChatHistory;

    /** Available Ollama models for dropdown. */
    TArray<TSharedPtr<FString>> AvailableModels;

    /** Currently selected model. */
    TSharedPtr<FString> CurrentModel;

    /** UI widget references. */
    TSharedPtr<SMultiLineEditableTextBox> InputTextBox;
    TSharedPtr<SScrollBox> MessageScrollBox;
    TSharedPtr<SVerticalBox> MessageListBox;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelComboBox;
    TSharedPtr<STextBlock> StatusText;

    /** Unique session id for this panel instance. */
    FString SessionId;

    /** Base URL of the local Python agent. */
    FString AgentBaseUrl;

    /** True while an HTTP request is in flight. */
    bool bIsBusy = false;

    /** Current job id being polled (empty when idle). */
    FString ActiveJobId;

    /** How long we've been polling the current job (seconds). */
    float ElapsedPollSeconds = 0.0f;

    // --- Streaming state ---

    /** The streaming assistant message widget being updated incrementally. */
    TSharedPtr<SVerticalBox> StreamingMessageBox;

    /** Length of partial text displayed so far (to avoid redundant rebuilds). */
    int32 LastPartialLength = 0;

    /** Update or create the streaming message widget with partial text. */
    void UpdateStreamingMessage(const FString& PartialText);

    /** Finalize the streaming message when the job is done. */
    void FinalizeStreamingMessage(const FString& FinalText);

    /** Add tool-call log entries to the chat. */
    void AddToolLogEntries(const TArray<TSharedPtr<FJsonValue>>& ToolLog);
};

// UnrealAI - Chat Panel Widget Implementation
#include "UI/SUnrealAIChatPanel.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Framework/Application/SlateApplication.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "UnrealAIChatPanel"

void SUnrealAIChatPanel::Construct(const FArguments& InArgs)
{
    // Unique session id for this panel instance (new GUID).
    SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

    // Agent base URL — reads env override UNREALAI_AGENT_URL if set.
    AgentBaseUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREALAI_AGENT_URL"));
    if (AgentBaseUrl.IsEmpty())
    {
        AgentBaseUrl = TEXT("http://127.0.0.1:8765");
    }
    AgentBaseUrl.RemoveFromEnd(TEXT("/"));

    // Initial model list (hardcoded fallback; /models endpoint will refresh).
    AvailableModels.Add(MakeShared<FString>(TEXT("qwen2.5-coder:32b")));
    AvailableModels.Add(MakeShared<FString>(TEXT("deepseek-coder-v2:16b")));
    AvailableModels.Add(MakeShared<FString>(TEXT("gemma4:latest")));
    CurrentModel = AvailableModels[0];

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(4.0f)
        [
            SNew(SVerticalBox)

            // ---- Header bar: model dropdown + status + clear button ----
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(4.0f, 2.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ModelLabel", "Model:"))
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SAssignNew(ModelComboBox, SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&AvailableModels)
                    .InitiallySelectedItem(CurrentModel)
                    .OnGenerateWidget(this, &SUnrealAIChatPanel::MakeModelComboWidget)
                    .OnSelectionChanged(this, &SUnrealAIChatPanel::OnModelChanged)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return CurrentModel.IsValid()
                                ? FText::FromString(*CurrentModel)
                                : LOCTEXT("NoModel", "Select a model");
                        })
                    ]
                ]

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                .Padding(12.0f, 0.0f)
                [
                    SAssignNew(StatusText, STextBlock)
                    .Text(LOCTEXT("StatusReady", "Ready"))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)))
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ClearButton", "Clear"))
                    .ToolTipText(LOCTEXT("ClearTooltip", "Clear chat history"))
                    .OnClicked(this, &SUnrealAIChatPanel::OnClearClicked)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("SaveButton", "Save"))
                    .ToolTipText(LOCTEXT("SaveTooltip", "Save this conversation"))
                    .OnClicked(this, &SUnrealAIChatPanel::OnSaveClicked)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("LoadButton", "Load"))
                    .ToolTipText(LOCTEXT("LoadTooltip", "Load a saved conversation"))
                    .OnClicked(this, &SUnrealAIChatPanel::OnLoadClicked)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("OpenVSCodeButton", "Open in VS Code"))
                    .ToolTipText(LOCTEXT("OpenVSCodeTooltip", "Launch VS Code on the UnrealAI workspace so you can chat with Copilot. Each VS Code window can drive this editor through the MCP server."))
                    .OnClicked(this, &SUnrealAIChatPanel::OnOpenInVSCodeClicked)
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SSeparator)
            ]

            // ---- Message list (scrollable) ----
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(4.0f)
            [
                SAssignNew(MessageScrollBox, SScrollBox)
                .ScrollBarAlwaysVisible(false)
                + SScrollBox::Slot()
                [
                    SAssignNew(MessageListBox, SVerticalBox)
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SSeparator)
            ]

            // ---- Input area ----
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(4.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SBox)
                    .MinDesiredHeight(60.0f)
                    .MaxDesiredHeight(160.0f)
                    [
                        SAssignNew(InputTextBox, SMultiLineEditableTextBox)
                        .HintText(LOCTEXT("InputHint", "Ask anything about your project... (Ctrl+Enter to send)"))
                        .OnTextCommitted(this, &SUnrealAIChatPanel::OnInputTextCommitted)
                        .AutoWrapText(true)
                        .ModiferKeyForNewLine(EModifierKey::None)
                    ]
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Bottom)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("SendButton", "Send"))
                    .ToolTipText(LOCTEXT("SendTooltip", "Send the message (Ctrl+Enter)"))
                    .OnClicked(this, &SUnrealAIChatPanel::OnSendClicked)
                    .ContentPadding(FMargin(12.0f, 4.0f))
                ]
            ]
        ]
    ];

    // Welcome message
    AddMessage(EChatMessageRole::System,
        FString::Printf(TEXT("UnrealAI ready. Agent URL: %s  •  Session: %s"),
            *AgentBaseUrl, *SessionId.Left(8)));

    // Fetch live model list from the agent (non-blocking; silent on failure).
    FetchAvailableModels();
}

TSharedRef<SWidget> SUnrealAIChatPanel::MakeModelComboWidget(TSharedPtr<FString> InItem)
{
    return SNew(STextBlock).Text(FText::FromString(InItem.IsValid() ? *InItem : FString()));
}

void SUnrealAIChatPanel::OnModelChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
    if (NewSelection.IsValid())
    {
        CurrentModel = NewSelection;
    }
}

FReply SUnrealAIChatPanel::OnSendClicked()
{
    if (!InputTextBox.IsValid() || bIsBusy)
    {
        return FReply::Handled();
    }

    const FString UserInput = InputTextBox->GetText().ToString().TrimStartAndEnd();
    if (UserInput.IsEmpty())
    {
        return FReply::Handled();
    }

    // Add user message
    AddMessage(EChatMessageRole::User, UserInput);

    // Clear input
    InputTextBox->SetText(FText::GetEmpty());

    // Dispatch to Python agent.
    SendMessageToAgent(UserInput);

    return FReply::Handled();
}

void SUnrealAIChatPanel::SendMessageToAgent(const FString& UserMessage)
{
    SetBusy(true);
    SetStatus(TEXT("Thinking..."), FLinearColor(0.7f, 0.7f, 1.0f));

    const FString ModelName = CurrentModel.IsValid() ? *CurrentModel : TEXT("qwen2.5-coder:32b");

    // Build JSON body.
    TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("message"), UserMessage);
    Body->SetStringField(TEXT("model"), ModelName);
    Body->SetStringField(TEXT("session_id"), SessionId);

    // Attach live project context so the agent knows which UE project is active.
    TSharedPtr<FJsonObject> Context = GatherProjectContext();
    if (Context.IsValid())
    {
        Body->SetObjectField(TEXT("context"), Context.ToSharedRef());
    }

    FString Payload;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    FJsonSerializer::Serialize(Body, Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("POST"));
    Request->SetURL(AgentBaseUrl + TEXT("/chat"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    // POST returns immediately with a job id; 30s is plenty.
    Request->SetTimeout(30.0f);
    Request->SetContentAsString(Payload);
    Request->OnProcessRequestComplete().BindSP(this, &SUnrealAIChatPanel::OnChatResponseReceived);
    Request->ProcessRequest();
}

void SUnrealAIChatPanel::OnChatResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
    if (!bSucceeded || !Response.IsValid())
    {
        SetBusy(false);
        const FString Err = FString::Printf(
            TEXT("Cannot reach agent at %s. Is `python unreal_ai_agent.py` running?"),
            *AgentBaseUrl);
        SetStatus(TEXT("Connection failed"), FLinearColor::Red);
        AddMessage(EChatMessageRole::System, Err);
        return;
    }

    const int32 Code = Response->GetResponseCode();
    const FString Raw = Response->GetContentAsString();

    if (Code < 200 || Code >= 300)
    {
        SetBusy(false);
        SetStatus(FString::Printf(TEXT("HTTP %d"), Code), FLinearColor::Red);
        AddMessage(EChatMessageRole::System,
            FString::Printf(TEXT("Agent returned HTTP %d:\n%s"), Code, *Raw));
        return;
    }

    TSharedPtr<FJsonObject> Obj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
    {
        SetBusy(false);
        SetStatus(TEXT("Bad response"), FLinearColor::Red);
        AddMessage(EChatMessageRole::System,
            FString::Printf(TEXT("Invalid JSON from agent:\n%s"), *Raw));
        return;
    }

    // Async job pattern: agent returns { job_id, status: "pending", ... }
    FString JobId;
    FString JobStatus;
    Obj->TryGetStringField(TEXT("job_id"), JobId);
    Obj->TryGetStringField(TEXT("status"), JobStatus);

    if (!JobId.IsEmpty() && JobStatus == TEXT("pending"))
    {
        ActiveJobId = JobId;
        ElapsedPollSeconds = 0.0f;
        SetStatus(TEXT("Thinking..."), FLinearColor(0.7f, 0.7f, 1.0f));
        // Begin polling.
        PollJobStatus(JobId);
        return;
    }

    // Fallback: synchronous response (older agent)
    const FString Reply = Obj->GetStringField(TEXT("response"));
    SetBusy(false);
    if (Reply.IsEmpty())
    {
        SetStatus(TEXT("Empty reply"), FLinearColor(1.0f, 0.6f, 0.0f));
        AddMessage(EChatMessageRole::System, TEXT("Agent returned an empty response."));
        return;
    }
    AddMessage(EChatMessageRole::Assistant, Reply);
    SetStatus(TEXT("Ready"), FLinearColor(0.5f, 1.0f, 0.5f));
}

void SUnrealAIChatPanel::PollJobStatus(const FString& JobId)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("GET"));
    Request->SetURL(AgentBaseUrl + TEXT("/chat/status/") + JobId);
    Request->SetTimeout(15.0f);

    // Bind a lambda that forwards to the member handler with the job id captured.
    TWeakPtr<SUnrealAIChatPanel> WeakSelf = StaticCastSharedRef<SUnrealAIChatPanel>(AsShared());
    Request->OnProcessRequestComplete().BindLambda(
        [WeakSelf, JobId](FHttpRequestPtr InReq, FHttpResponsePtr InResp, bool bOk)
        {
            if (TSharedPtr<SUnrealAIChatPanel> Pinned = WeakSelf.Pin())
            {
                Pinned->OnJobStatusReceived(InReq, InResp, bOk, JobId);
            }
        });
    Request->ProcessRequest();
}

void SUnrealAIChatPanel::OnJobStatusReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, FString JobId)
{
    // Drop stale responses (e.g. user clicked Clear mid-flight).
    if (JobId != ActiveJobId)
    {
        return;
    }

    // Network failure on a status poll — keep retrying up to ~10 minutes total.
    const float MaxPollSeconds = 600.0f;
    if (!bSucceeded || !Response.IsValid())
    {
        if (ElapsedPollSeconds >= MaxPollSeconds)
        {
            ActiveJobId.Empty();
            SetBusy(false);
            SetStatus(TEXT("Poll failed"), FLinearColor::Red);
            AddMessage(EChatMessageRole::System,
                FString::Printf(TEXT("Lost connection while waiting for response from %s."), *AgentBaseUrl));
            return;
        }
        // schedule a retry via ticker
        TWeakPtr<SUnrealAIChatPanel> WeakSelf = StaticCastSharedRef<SUnrealAIChatPanel>(AsShared());
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [WeakSelf, JobId](float DeltaTime) -> bool
            {
                if (TSharedPtr<SUnrealAIChatPanel> Pinned = WeakSelf.Pin())
                {
                    if (Pinned->ActiveJobId == JobId)
                    {
                        Pinned->ElapsedPollSeconds += 2.0f;
                        Pinned->PollJobStatus(JobId);
                    }
                }
                return false; // one-shot
            }), 2.0f);
        return;
    }

    const int32 Code = Response->GetResponseCode();
    const FString Raw = Response->GetContentAsString();

    if (Code == 404)
    {
        ActiveJobId.Empty();
        SetBusy(false);
        SetStatus(TEXT("Job missing"), FLinearColor::Red);
        AddMessage(EChatMessageRole::System, TEXT("Agent no longer knows about this job."));
        return;
    }
    if (Code < 200 || Code >= 300)
    {
        ActiveJobId.Empty();
        SetBusy(false);
        SetStatus(FString::Printf(TEXT("HTTP %d"), Code), FLinearColor::Red);
        AddMessage(EChatMessageRole::System,
            FString::Printf(TEXT("Poll returned HTTP %d: %s"), Code, *Raw));
        return;
    }

    TSharedPtr<FJsonObject> Obj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
    {
        ActiveJobId.Empty();
        SetBusy(false);
        SetStatus(TEXT("Bad JSON"), FLinearColor::Red);
        return;
    }

    FString JobStatus;
    Obj->TryGetStringField(TEXT("status"), JobStatus);

    if (JobStatus == TEXT("done"))
    {
        ActiveJobId.Empty();
        SetBusy(false);
        const FString Reply = Obj->GetStringField(TEXT("response"));

        // Capture tool log before finalize (AddToolLogEntries is called after
        // FinalizeStreamingMessage so RefreshMessageList doesn't wipe them).
        const TArray<TSharedPtr<FJsonValue>>* ToolLogArray = nullptr;
        bool bHasToolLog = Obj->TryGetArrayField(TEXT("tool_log"), ToolLogArray)
                           && ToolLogArray && ToolLogArray->Num() > 0;

        if (Reply.IsEmpty())
        {
            // Remove streaming widget if any.
            FinalizeStreamingMessage(FString());
            if (bHasToolLog) { AddToolLogEntries(*ToolLogArray); }
            SetStatus(TEXT("Empty reply"), FLinearColor(1.0f, 0.6f, 0.0f));
            AddMessage(EChatMessageRole::System, TEXT("Agent returned an empty response."));
        }
        else
        {
            // Replace streaming widget with final message.
            FinalizeStreamingMessage(Reply);
            if (bHasToolLog) { AddToolLogEntries(*ToolLogArray); }
            SetStatus(TEXT("Ready"), FLinearColor(0.5f, 1.0f, 0.5f));
        }
        return;
    }
    if (JobStatus == TEXT("error"))
    {
        ActiveJobId.Empty();
        SetBusy(false);
        // Clean up any streaming widget.
        StreamingMessageBox.Reset();
        LastPartialLength = 0;
        const FString Err = Obj->GetStringField(TEXT("response"));
        SetStatus(TEXT("Agent error"), FLinearColor::Red);
        AddMessage(EChatMessageRole::System,
            FString::Printf(TEXT("Agent error: %s"), Err.IsEmpty() ? TEXT("(unknown)") : *Err));
        return;
    }

    // Still pending — update streaming display and schedule another poll.
    if (ElapsedPollSeconds < MaxPollSeconds)
    {
        // Show partial text if available (streaming).
        FString PartialText;
        Obj->TryGetStringField(TEXT("partial_text"), PartialText);
        if (!PartialText.IsEmpty())
        {
            UpdateStreamingMessage(PartialText);
            SetStatus(FString::Printf(TEXT("Streaming... (%.0fs)"), ElapsedPollSeconds),
                      FLinearColor(0.3f, 0.8f, 1.0f));
        }
        else
        {
            SetStatus(FString::Printf(TEXT("Thinking... (%.0fs)"), ElapsedPollSeconds),
                      FLinearColor(0.7f, 0.7f, 1.0f));
        }

        TWeakPtr<SUnrealAIChatPanel> WeakSelf = StaticCastSharedRef<SUnrealAIChatPanel>(AsShared());
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [WeakSelf, JobId](float DeltaTime) -> bool
            {
                if (TSharedPtr<SUnrealAIChatPanel> Pinned = WeakSelf.Pin())
                {
                    if (Pinned->ActiveJobId == JobId)
                    {
                        Pinned->ElapsedPollSeconds += 1.0f;
                        Pinned->PollJobStatus(JobId);
                    }
                }
                return false;
            }), 1.0f);
    }
    else
    {
        ActiveJobId.Empty();
        SetBusy(false);
        // Clean up any streaming widget.
        StreamingMessageBox.Reset();
        LastPartialLength = 0;
        SetStatus(TEXT("Timed out"), FLinearColor::Red);
        AddMessage(EChatMessageRole::System,
            TEXT("Agent did not finish within 10 minutes. Try a smaller model or shorter prompt."));
    }
}

void SUnrealAIChatPanel::FetchAvailableModels()
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("GET"));
    Request->SetURL(AgentBaseUrl + TEXT("/models"));
    Request->SetTimeout(10.0f);
    Request->OnProcessRequestComplete().BindSP(this, &SUnrealAIChatPanel::OnModelsResponseReceived);
    Request->ProcessRequest();
}

void SUnrealAIChatPanel::OnModelsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
    if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
    {
        // Silent: keep fallback model list, just flag status.
        SetStatus(TEXT("Agent offline (using fallback model list)"), FLinearColor(1.0f, 0.6f, 0.0f));
        return;
    }

    TSharedPtr<FJsonObject> Obj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
    {
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
    if (!Obj->TryGetArrayField(TEXT("models"), Models) || !Models || Models->Num() == 0)
    {
        return;
    }

    AvailableModels.Empty();
    for (const TSharedPtr<FJsonValue>& V : *Models)
    {
        if (V.IsValid() && V->Type == EJson::String)
        {
            AvailableModels.Add(MakeShared<FString>(V->AsString()));
        }
    }

    if (AvailableModels.Num() > 0)
    {
        // Preserve current selection if still present, otherwise pick first.
        TSharedPtr<FString> NewCurrent = AvailableModels[0];
        if (CurrentModel.IsValid())
        {
            for (const TSharedPtr<FString>& M : AvailableModels)
            {
                if (M.IsValid() && *M == *CurrentModel)
                {
                    NewCurrent = M;
                    break;
                }
            }
        }
        CurrentModel = NewCurrent;

        if (ModelComboBox.IsValid())
        {
            ModelComboBox->RefreshOptions();
            ModelComboBox->SetSelectedItem(CurrentModel);
        }
    }

    SetStatus(TEXT("Ready"), FLinearColor(0.5f, 1.0f, 0.5f));
}

void SUnrealAIChatPanel::SetStatus(const FString& Text, const FLinearColor& Color)
{
    if (StatusText.IsValid())
    {
        StatusText->SetText(FText::FromString(Text));
        StatusText->SetColorAndOpacity(FSlateColor(Color));
    }
}

void SUnrealAIChatPanel::SetBusy(bool bBusy)
{
    bIsBusy = bBusy;
    if (InputTextBox.IsValid())
    {
        InputTextBox->SetEnabled(!bBusy);
    }
}

void SUnrealAIChatPanel::OnInputTextCommitted(const FText& InText, ETextCommit::Type CommitType)
{
    if (CommitType == ETextCommit::OnEnter)
    {
        // Only send on Ctrl+Enter; plain Enter adds newline.
        const FModifierKeysState ModKeys = FSlateApplication::Get().GetModifierKeys();
        if (ModKeys.IsControlDown())
        {
            OnSendClicked();
        }
    }
}

FReply SUnrealAIChatPanel::OnClearClicked()
{
    // Cancel any in-flight job so the response doesn't arrive later.
    ActiveJobId.Empty();
    SetBusy(false);
    // Clean up streaming state.
    StreamingMessageBox.Reset();
    LastPartialLength = 0;
    ChatHistory.Empty();
    RefreshMessageList();
    AddMessage(EChatMessageRole::System, TEXT("Chat cleared."));
    return FReply::Handled();
}

FReply SUnrealAIChatPanel::OnSaveClicked()
{
    if (ChatHistory.Num() <= 1)  // only the welcome message
    {
        SetStatus(TEXT("Nothing to save"), FLinearColor(1.0f, 0.6f, 0.0f));
        return FReply::Handled();
    }

    // POST /session/save?session_id=...
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("POST"));
    Request->SetURL(FString::Printf(TEXT("%s/session/save?session_id=%s"), *AgentBaseUrl, *SessionId));
    Request->SetTimeout(10.0f);

    TWeakPtr<SUnrealAIChatPanel> WeakSelf = StaticCastSharedRef<SUnrealAIChatPanel>(AsShared());
    Request->OnProcessRequestComplete().BindLambda(
        [WeakSelf](FHttpRequestPtr InReq, FHttpResponsePtr InResp, bool bOk)
        {
            TSharedPtr<SUnrealAIChatPanel> Pinned = WeakSelf.Pin();
            if (!Pinned) return;

            if (!bOk || !InResp.IsValid() || InResp->GetResponseCode() >= 400)
            {
                Pinned->SetStatus(TEXT("Save failed"), FLinearColor::Red);
                return;
            }
            Pinned->SetStatus(TEXT("Conversation saved"), FLinearColor(0.5f, 1.0f, 0.5f));
            Pinned->AddMessage(EChatMessageRole::System, TEXT("Conversation saved."));
        });
    Request->ProcessRequest();
    return FReply::Handled();
}

FReply SUnrealAIChatPanel::OnLoadClicked()
{
    // First fetch the list of saved sessions.
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("GET"));
    Request->SetURL(AgentBaseUrl + TEXT("/sessions"));
    Request->SetTimeout(10.0f);

    TWeakPtr<SUnrealAIChatPanel> WeakSelf = StaticCastSharedRef<SUnrealAIChatPanel>(AsShared());
    Request->OnProcessRequestComplete().BindLambda(
        [WeakSelf](FHttpRequestPtr InReq, FHttpResponsePtr InResp, bool bOk)
        {
            TSharedPtr<SUnrealAIChatPanel> Pinned = WeakSelf.Pin();
            if (!Pinned) return;

            if (!bOk || !InResp.IsValid() || InResp->GetResponseCode() >= 400)
            {
                Pinned->SetStatus(TEXT("Cannot list saves"), FLinearColor::Red);
                return;
            }

            TSharedPtr<FJsonObject> Obj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InResp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
            {
                return;
            }

            const TArray<TSharedPtr<FJsonValue>>* Sessions = nullptr;
            if (!Obj->TryGetArrayField(TEXT("sessions"), Sessions) || !Sessions || Sessions->Num() == 0)
            {
                Pinned->SetStatus(TEXT("No saved conversations"), FLinearColor(1.0f, 0.6f, 0.0f));
                return;
            }

            // Load the most recent session automatically.
            // (A full session-picker dialog is Phase 7 scope.)
            const TSharedPtr<FJsonObject>* First = nullptr;
            if ((*Sessions)[0]->TryGetObject(First) && (*First).IsValid())
            {
                FString Filename = (*First)->GetStringField(TEXT("filename"));
                Pinned->LoadSession(Filename);
            }
        });
    Request->ProcessRequest();
    return FReply::Handled();
}

FReply SUnrealAIChatPanel::OnOpenInVSCodeClicked()
{
    // Goal: open the *Unreal project folder itself* in a brand-new VS Code
    // window so each UE project gets an isolated, persistent VS Code workspace
    // (chat history, MCP server scope, settings) bound to it.
    //
    // Steps:
    //   1. Resolve the UE project's root dir.
    //   2. Make sure that folder has the two config files VS Code Copilot
    //      needs to talk to *this* editor:
    //         - .vscode/mcp.json       (registers the unrealai MCP server)
    //         - .claude/agents/unreal-mcp.md (custom Copilot agent)
    //   3. Launch `code "<projectDir>" --new-window`.
    //
    // The MCP server script + venv live centrally in the dev workspace; we
    // just point each per-project .vscode/mcp.json at those absolute paths.

    const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

    // --- absolute paths to the central Python venv + MCP server script ---
    // (TODO: move to UDeveloperSettings so users can edit from Project Settings.)
    static const FString PythonExe = TEXT("e:\\\\unrealBP\\\\.venv\\\\Scripts\\\\python.exe");
    static const FString McpScript = TEXT("e:\\\\unrealBP\\\\UnrealAI\\\\Python\\\\unreal_ai_mcp.py");

    // --- ensure .vscode/mcp.json ---
    const FString VsCodeDir = FPaths::Combine(ProjectDir, TEXT(".vscode"));
    const FString McpJsonPath = FPaths::Combine(VsCodeDir, TEXT("mcp.json"));
    IFileManager::Get().MakeDirectory(*VsCodeDir, /*Tree*/ true);
    if (!FPaths::FileExists(McpJsonPath))
    {
        const FString McpJson = FString::Printf(TEXT(
            "{\n"
            "    \"servers\": {\n"
            "        \"unrealai\": {\n"
            "            \"type\": \"stdio\",\n"
            "            \"command\": \"%s\",\n"
            "            \"args\": [\"%s\"]\n"
            "        }\n"
            "    }\n"
            "}\n"),
            *PythonExe, *McpScript);
        FFileHelper::SaveStringToFile(McpJson, *McpJsonPath);
    }

    // --- ensure .claude/agents/unreal-mcp.md ---
    const FString AgentDir  = FPaths::Combine(ProjectDir, TEXT(".claude"), TEXT("agents"));
    const FString AgentPath = FPaths::Combine(AgentDir, TEXT("unreal-mcp.md"));
    IFileManager::Get().MakeDirectory(*AgentDir, /*Tree*/ true);
    if (!FPaths::FileExists(AgentPath))
    {
        // Keep this body short: the canonical, full cookbook lives in the
        // dev workspace at e:\unrealBP\.claude\agents\unreal-mcp.md and can
        // be copied into a project that needs the long form.
        const FString AgentBody = TEXT(
            "---\n"
            "name: unreal-mcp\n"
            "description: \"Drives the open Unreal Editor through the UnrealAI MCP server. Use for actor spawning/inspection, blueprint reading/editing, level introspection, and Unreal concept questions.\"\n"
            "model: claude-sonnet-4.5\n"
            "---\n"
            "\n"
            "# Unreal MCP \u2014 Editor Co-pilot\n"
            "\n"
            "You are an Unreal Engine assistant connected to a live editor session through the `unrealai` MCP server (TCP 55557). You can both answer Unreal questions AND act on the open project.\n"
            "\n"
            "## Operating principles\n"
            "1. Talk first, act second \u2014 only call tools when the user wants editor state read or changed.\n"
            "2. Call `ping` once at the start of a session to verify the bridge.\n"
            "3. Inspect before mutating: `read_blueprint_content` / `analyze_blueprint_graph` before editing a Blueprint.\n"
            "4. Compile after Blueprint edits with `compile_blueprint`.\n"
            "5. Surface tool errors clearly, don't silently retry.\n"
            "\n"
            "## End-to-end Blueprint workflow\n"
            "1. **Create** (only when the BP doesn't exist) \u2014 `create_blueprint(name, parent_class)`.\n"
            "2. **Inspect** \u2014 `read_blueprint_content`, `analyze_blueprint_graph`.\n"
            "3. **Components** if needed \u2014 `add_component_to_blueprint`.\n"
            "4. **Graph logic** \u2014 assemble T3D from snippets and call `paste_blueprint_graph`. Pass `clear_graph=true` only when explicitly asked to wipe.\n"
            "5. **Compile** \u2014 `compile_blueprint`.\n"
            "6. **Verify** \u2014 re-run `analyze_blueprint_graph` and report what changed.\n"
            "\n"
            "If `paste_blueprint_graph` returns `status=\"validation_failed\"`, the response includes `validation_errors` (code + message + node/pin). Fix every error and resend the FULL T3D \u2014 never partial-patch.\n"
            "\n"
            "## Tool quick-reference\n"
            "- Health: `ping`\n"
            "- Level: `get_actors_in_level`, `find_actors_by_name`, `spawn_actor`, `set_actor_transform`, `delete_actor`\n"
            "- Blueprint create: `create_blueprint`, `compile_blueprint`, `add_component_to_blueprint`\n"
            "- Blueprint inspect: `read_blueprint_content`, `analyze_blueprint_graph`, `get_blueprint_variable_details`, `get_blueprint_function_details`\n"
            "- Blueprint author: `paste_blueprint_graph` (T3D paste)\n"
            "- T3D snippet library: `list_t3d_snippets`, `get_t3d_snippet`\n"
            "- Materials: `get_available_materials`\n"
            "\n"
            "## T3D cookbook (short form)\n"
            "1. `list_t3d_snippets` to see what's available (event_begin_play, event_tick, print_string, branch, sequence, get_variable, set_variable, cast, call_function, for_loop).\n"
            "2. `get_t3d_snippet(name)` once per node \u2014 each call returns paste-safe T3D with fresh GUIDs, plus `node_name` and `pin_names` for wiring.\n"
            "3. Wire pins by editing `LinkedTo=(NodeName PinId,...)` in the relevant CustomProperties Pin lines. Bidirectional links must be added on BOTH ends.\n"
            "4. Concatenate snippet `t3d_text` blocks into one string, call `paste_blueprint_graph`.\n");
        FFileHelper::SaveStringToFile(AgentBody, *AgentPath);
    }

    // --- launch VS Code in a fresh window pointing at the project dir ---
    // Quote the path so spaces are handled. --new-window forces a new window
    // even if VS Code is already running on something else.
    const FString CmdExe  = TEXT("cmd.exe");
    const FString CmdArgs = FString::Printf(
        TEXT("/c code \"%s\" --new-window"), *ProjectDir);

    FProcHandle Handle = FPlatformProcess::CreateProc(
        *CmdExe, *CmdArgs,
        /*bLaunchDetached*/ true,
        /*bLaunchHidden*/   true,
        /*bLaunchReallyHidden*/ true,
        /*OutProcessID*/    nullptr,
        /*PriorityModifier*/ 0,
        /*OptionalWorkingDirectory*/ nullptr,
        /*PipeWriteChild*/  nullptr,
        /*PipeReadChild*/   nullptr);

    if (!Handle.IsValid())
    {
        SetStatus(TEXT("Could not launch VS Code (is 'code' on PATH?)"),
                  FLinearColor::Red);
        AddMessage(EChatMessageRole::System,
            TEXT("**Open in VS Code** failed.\n\n"
                 "Make sure VS Code's `code` command is on your PATH.\n"
                 "In VS Code, run **Shell Command: Install 'code' command in PATH** from the Command Palette."));
        return FReply::Handled();
    }
    FPlatformProcess::CloseProc(Handle);

    SetStatus(TEXT("VS Code launched"), FLinearColor(0.5f, 1.0f, 0.5f));
    AddMessage(EChatMessageRole::System,
        FString::Printf(TEXT(
            "Opened **%s** in a new VS Code window.\n\n"
            "Provisioned (if missing):\n"
            "- `.vscode/mcp.json` \u2014 registers the **unrealai** MCP server for this project.\n"
            "- `.claude/agents/unreal-mcp.md` \u2014 custom Copilot agent scoped to Unreal tools.\n\n"
            "Inside the new window:\n"
            "1. Open Copilot Chat (Ctrl+Alt+I).\n"
            "2. In the Agent dropdown (bottom of the chat input) pick **unreal-mcp**.\n"
            "3. Ask things like *\"List actors in the level\"* or *\"Spawn a cube at 0,0,200\"*.\n\n"
            "Each VS Code window stays bound to its own project folder, so chat history and sessions persist per-project."),
            *ProjectDir));

    return FReply::Handled();
}

void SUnrealAIChatPanel::LoadSession(const FString& Filename)
{
    // POST /session/load?filename=...&session_id=...
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("POST"));
    Request->SetURL(FString::Printf(TEXT("%s/session/load?filename=%s&session_id=%s"),
        *AgentBaseUrl, *Filename, *SessionId));
    Request->SetTimeout(15.0f);

    TWeakPtr<SUnrealAIChatPanel> WeakSelf = StaticCastSharedRef<SUnrealAIChatPanel>(AsShared());
    Request->OnProcessRequestComplete().BindLambda(
        [WeakSelf, Filename](FHttpRequestPtr InReq, FHttpResponsePtr InResp, bool bOk)
        {
            TSharedPtr<SUnrealAIChatPanel> Pinned = WeakSelf.Pin();
            if (!Pinned) return;

            if (!bOk || !InResp.IsValid() || InResp->GetResponseCode() >= 400)
            {
                Pinned->SetStatus(TEXT("Load failed"), FLinearColor::Red);
                return;
            }

            TSharedPtr<FJsonObject> Obj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InResp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
            {
                return;
            }

            // Rebuild ChatHistory from the loaded messages.
            const TArray<TSharedPtr<FJsonValue>>* History = nullptr;
            if (!Obj->TryGetArrayField(TEXT("history"), History) || !History)
            {
                return;
            }

            Pinned->ChatHistory.Empty();
            for (const TSharedPtr<FJsonValue>& MsgVal : *History)
            {
                const TSharedPtr<FJsonObject>* MsgObj = nullptr;
                if (!MsgVal->TryGetObject(MsgObj) || !(*MsgObj).IsValid())
                {
                    continue;
                }

                FString Role = (*MsgObj)->GetStringField(TEXT("role"));
                FString Content = (*MsgObj)->GetStringField(TEXT("content"));

                EChatMessageRole ChatRole = EChatMessageRole::System;
                if (Role == TEXT("user"))
                {
                    ChatRole = EChatMessageRole::User;
                }
                else if (Role == TEXT("assistant"))
                {
                    ChatRole = EChatMessageRole::Assistant;
                }
                else if (Role == TEXT("tool"))
                {
                    continue; // skip tool results in display
                }

                Pinned->ChatHistory.Emplace(ChatRole, Content);
            }

            Pinned->RefreshMessageList();
            Pinned->AddMessage(EChatMessageRole::System,
                FString::Printf(TEXT("Loaded conversation: %s"), *Filename));
            Pinned->SetStatus(TEXT("Conversation loaded"), FLinearColor(0.5f, 1.0f, 0.5f));
        });
    Request->ProcessRequest();
}

void SUnrealAIChatPanel::AddMessage(EChatMessageRole Role, const FString& Content)
{
    ChatHistory.Emplace(Role, Content);

    if (MessageListBox.IsValid())
    {
        MessageListBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 3.0f)
        [
            CreateMessageWidget(ChatHistory.Last())
        ];

        ScrollToBottom();
    }
}

void SUnrealAIChatPanel::UpdateStreamingMessage(const FString& PartialText)
{
    if (!MessageListBox.IsValid() || PartialText.IsEmpty())
    {
        return;
    }

    // Only rebuild if partial text actually changed.
    if (PartialText.Len() == LastPartialLength)
    {
        return;
    }
    LastPartialLength = PartialText.Len();

    if (!StreamingMessageBox.IsValid())
    {
        // Create a new streaming message container.
        StreamingMessageBox = SNew(SVerticalBox);

        MessageListBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 3.0f)
        [
            SNew(SBorder)
            .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.15f, 1.0f))
            .Padding(FMargin(10.0f, 8.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("AI (streaming...)")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.8f, 1.0f)))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    StreamingMessageBox.ToSharedRef()
                ]
            ]
        ];
    }

    // Rebuild the content inside the streaming box.
    StreamingMessageBox->ClearChildren();

    // Build segments from partial text (reuse existing splitter).
    FChatMessage TempMsg(EChatMessageRole::Assistant, PartialText);
    TArray<FChatContentSegment> Segments = SplitMessageIntoSegments(PartialText);

    for (const FChatContentSegment& Seg : Segments)
    {
        if (Seg.bIsCode)
        {
            StreamingMessageBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                CreateCodeBlockWidget(Seg.Text, Seg.Language)
            ];
        }
        else
        {
            StreamingMessageBox->AddSlot()
            .AutoHeight()
            [
                CreateProseWidget(Seg.Text)
            ];
        }
    }

    ScrollToBottom();
}

void SUnrealAIChatPanel::FinalizeStreamingMessage(const FString& FinalText)
{
    const bool bWasStreaming = StreamingMessageBox.IsValid();

    if (bWasStreaming)
    {
        StreamingMessageBox.Reset();
        // Rebuild the message list to remove the streaming placeholder.
        RefreshMessageList();
    }

    LastPartialLength = 0;

    if (!FinalText.IsEmpty())
    {
        AddMessage(EChatMessageRole::Assistant, FinalText);
    }
}

void SUnrealAIChatPanel::AddToolLogEntries(const TArray<TSharedPtr<FJsonValue>>& ToolLog)
{
    if (!MessageListBox.IsValid() || ToolLog.Num() == 0)
    {
        return;
    }

    // Build a single collapsible tool-log summary.
    FString Summary = FString::Printf(TEXT("[%d tool call(s)]"), ToolLog.Num());
    FString Details;

    for (int32 i = 0; i < ToolLog.Num(); ++i)
    {
        const TSharedPtr<FJsonObject>* EntryObj = nullptr;
        if (!ToolLog[i]->TryGetObject(EntryObj) || !(*EntryObj).IsValid())
        {
            continue;
        }

        FString Name = (*EntryObj)->GetStringField(TEXT("name"));
        FString Result = (*EntryObj)->GetStringField(TEXT("result"));
        if (Result.Len() > 200)
        {
            Result = Result.Left(200) + TEXT("...");
        }

        Details += FString::Printf(TEXT("  %d. %s -> %s\n"), i + 1, *Name, *Result);
    }

    // Add a system-style message showing tool calls.
    FString ToolText = Summary + TEXT("\n") + Details;

    MessageListBox->AddSlot()
    .AutoHeight()
    .Padding(0.0f, 2.0f)
    [
        SNew(SBorder)
        .BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.10f, 0.8f))
        .Padding(FMargin(8.0f, 4.0f))
        [
            SNew(STextBlock)
            .Text(FText::FromString(ToolText))
            .Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.5f)))
            .AutoWrapText(true)
        ]
    ];
}

void SUnrealAIChatPanel::RefreshMessageList()
{
    if (!MessageListBox.IsValid())
    {
        return;
    }

    MessageListBox->ClearChildren();
    for (const FChatMessage& Msg : ChatHistory)
    {
        MessageListBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 3.0f)
        [
            CreateMessageWidget(Msg)
        ];
    }
    ScrollToBottom();
}

TSharedRef<SWidget> SUnrealAIChatPanel::CreateMessageWidget(const FChatMessage& Message)
{
    // Role label + color
    FText RoleLabel;
    FLinearColor LabelColor;
    FLinearColor BgColor;

    switch (Message.Role)
    {
        case EChatMessageRole::User:
            RoleLabel = LOCTEXT("RoleUser", "You");
            LabelColor = FLinearColor(0.4f, 0.7f, 1.0f);
            BgColor = FLinearColor(0.10f, 0.12f, 0.16f, 0.6f);
            break;
        case EChatMessageRole::Assistant:
            RoleLabel = LOCTEXT("RoleAssistant", "UnrealAI");
            LabelColor = FLinearColor(0.5f, 0.9f, 0.6f);
            BgColor = FLinearColor(0.10f, 0.15f, 0.12f, 0.6f);
            break;
        case EChatMessageRole::System:
        default:
            RoleLabel = LOCTEXT("RoleSystem", "System");
            LabelColor = FLinearColor(0.8f, 0.8f, 0.5f);
            BgColor = FLinearColor(0.14f, 0.14f, 0.10f, 0.6f);
            break;
    }

    // Body: split content into prose + fenced code blocks. Each segment becomes
    // its own widget so long code (T3D, JSON, etc.) lives in a scrollable
    // monospace box with a Copy button instead of being truncated by the bubble.
    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
    TArray<FChatContentSegment> Segments = SplitMessageIntoSegments(Message.Content);
    for (const FChatContentSegment& Seg : Segments)
    {
        Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 3.0f, 0.0f, 0.0f)
            [
                Seg.bIsCode ? CreateCodeBlockWidget(Seg.Text, Seg.Language)
                            : CreateProseWidget(Seg.Text)
            ];
    }

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(FSlateColor(BgColor))
        .Padding(FMargin(8.0f, 6.0f))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(RoleLabel)
                .ColorAndOpacity(FSlateColor(LabelColor))
                .TextStyle(FAppStyle::Get(), "NormalText.Important")
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                Body
            ]
        ];
}

void SUnrealAIChatPanel::ScrollToBottom()
{
    if (MessageScrollBox.IsValid())
    {
        MessageScrollBox->ScrollToEnd();
    }
}

TSharedPtr<FJsonObject> SUnrealAIChatPanel::GatherProjectContext() const
{
    TSharedPtr<FJsonObject> Ctx = MakeShared<FJsonObject>();

    // Basic project info
    Ctx->SetStringField(TEXT("project_name"), FApp::GetProjectName());
    Ctx->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
    Ctx->SetStringField(TEXT("content_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
    Ctx->SetStringField(TEXT("source_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source")));
    Ctx->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());

    // Editor world / current map
    if (GEditor)
    {
        if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
        {
            Ctx->SetStringField(TEXT("current_level"), EditorWorld->GetMapName());
            if (EditorWorld->GetCurrentLevel())
            {
                Ctx->SetNumberField(TEXT("actor_count"), EditorWorld->GetCurrentLevel()->Actors.Num());
            }
        }

        // Selected actors
        TArray<TSharedPtr<FJsonValue>> SelectedArray;
        if (USelection* SelectedActors = GEditor->GetSelectedActors())
        {
            for (FSelectionIterator It(*SelectedActors); It; ++It)
            {
                if (AActor* Actor = Cast<AActor>(*It))
                {
                    TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
                    ActorObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
                    ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
                    const FVector Loc = Actor->GetActorLocation();
                    ActorObj->SetStringField(TEXT("location"),
                        FString::Printf(TEXT("%.1f, %.1f, %.1f"), Loc.X, Loc.Y, Loc.Z));
                    SelectedArray.Add(MakeShared<FJsonValueObject>(ActorObj));
                }
            }
        }
        Ctx->SetArrayField(TEXT("selected_actors"), SelectedArray);
    }

    // Top-level Content folders (one level deep, names only).
    {
        TArray<FString> Folders;
        IFileManager::Get().FindFiles(Folders, *(FPaths::ProjectContentDir() / TEXT("*")), false, true);
        TArray<TSharedPtr<FJsonValue>> FoldersJson;
        for (const FString& F : Folders)
        {
            FoldersJson.Add(MakeShared<FJsonValueString>(F));
        }
        Ctx->SetArrayField(TEXT("content_top_folders"), FoldersJson);
    }

    // Top-level Source folders (C++ modules).
    {
        TArray<FString> Modules;
        IFileManager::Get().FindFiles(Modules, *(FPaths::ProjectDir() / TEXT("Source") / TEXT("*")), false, true);
        TArray<TSharedPtr<FJsonValue>> ModulesJson;
        for (const FString& M : Modules)
        {
            ModulesJson.Add(MakeShared<FJsonValueString>(M));
        }
        Ctx->SetArrayField(TEXT("source_modules"), ModulesJson);
    }

    // Enabled plugins (project-level, not engine).
    {
        TArray<TSharedPtr<FJsonValue>> PluginsJson;
        const TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPlugins();
        for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
        {
            if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project)
            {
                PluginsJson.Add(MakeShared<FJsonValueString>(Plugin->GetName()));
            }
        }
        Ctx->SetArrayField(TEXT("project_plugins"), PluginsJson);
    }

    return Ctx;
}

// --------------------------------------------------------------------------- //
// Code-block / prose rendering
// --------------------------------------------------------------------------- //

TArray<FChatContentSegment> SUnrealAIChatPanel::SplitMessageIntoSegments(const FString& Content)
{
    TArray<FChatContentSegment> Out;
    if (Content.IsEmpty())
    {
        return Out;
    }

    const FString Fence(TEXT("```"));
    int32 Cursor = 0;
    while (Cursor < Content.Len())
    {
        int32 FenceStart = Content.Find(Fence, ESearchCase::IgnoreCase, ESearchDir::FromStart, Cursor);
        if (FenceStart == INDEX_NONE)
        {
            // No more fences — the rest is prose.
            FString Tail = Content.Mid(Cursor);
            if (!Tail.IsEmpty())
            {
                FChatContentSegment Seg;
                Seg.Text = Tail;
                Seg.bIsCode = false;
                Out.Add(MoveTemp(Seg));
            }
            break;
        }

        // Emit any prose that came before the fence.
        if (FenceStart > Cursor)
        {
            FChatContentSegment Seg;
            Seg.Text = Content.Mid(Cursor, FenceStart - Cursor);
            Seg.bIsCode = false;
            Out.Add(MoveTemp(Seg));
        }

        // Fence open: optional language tag on same line, code body until next fence.
        int32 LangEnd = Content.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, FenceStart + Fence.Len());
        if (LangEnd == INDEX_NONE)
        {
            // Unterminated open fence — treat the rest as code with no language.
            FChatContentSegment Seg;
            Seg.Text = Content.Mid(FenceStart + Fence.Len());
            Seg.bIsCode = true;
            Out.Add(MoveTemp(Seg));
            break;
        }

        FString Language = Content.Mid(FenceStart + Fence.Len(), LangEnd - (FenceStart + Fence.Len())).TrimStartAndEnd();
        int32 CodeStart = LangEnd + 1;
        int32 FenceClose = Content.Find(Fence, ESearchCase::IgnoreCase, ESearchDir::FromStart, CodeStart);
        if (FenceClose == INDEX_NONE)
        {
            FChatContentSegment Seg;
            Seg.Text = Content.Mid(CodeStart);
            Seg.Language = Language;
            Seg.bIsCode = true;
            Out.Add(MoveTemp(Seg));
            break;
        }

        FChatContentSegment Seg;
        Seg.Text = Content.Mid(CodeStart, FenceClose - CodeStart);
        Seg.Language = Language;
        Seg.bIsCode = true;
        // Trim a single trailing newline so the box doesn't show a blank line.
        if (Seg.Text.EndsWith(TEXT("\n")))
        {
            Seg.Text = Seg.Text.LeftChop(1);
        }
        Out.Add(MoveTemp(Seg));

        Cursor = FenceClose + Fence.Len();
        // Skip an immediate trailing newline after the closing fence.
        if (Cursor < Content.Len() && Content[Cursor] == TEXT('\n'))
        {
            ++Cursor;
        }
    }

    return Out;
}

TSharedRef<SWidget> SUnrealAIChatPanel::CreateProseWidget(const FString& Text)
{
    // Parse lightweight markdown: headers, bold lines, bullet lists, blockquotes.
    // Each line becomes its own STextBlock with appropriate styling.

    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines, false);

    // Single-line fast path (no markdown structure to render).
    if (Lines.Num() <= 1)
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Text))
            .AutoWrapText(true);
    }

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    for (const FString& RawLine : Lines)
    {
        FString Line = RawLine;

        // --- Headers: # H1, ## H2, ### H3 ---
        if (Line.StartsWith(TEXT("### ")))
        {
            Box->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Line.Mid(4)))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                .AutoWrapText(true)
            ];
            continue;
        }
        if (Line.StartsWith(TEXT("## ")))
        {
            Box->AddSlot().AutoHeight().Padding(0, 5, 0, 2)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Line.Mid(3)))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                .AutoWrapText(true)
            ];
            continue;
        }
        if (Line.StartsWith(TEXT("# ")))
        {
            Box->AddSlot().AutoHeight().Padding(0, 6, 0, 3)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Line.Mid(2)))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                .AutoWrapText(true)
            ];
            continue;
        }

        // --- Blockquote: > text ---
        if (Line.StartsWith(TEXT("> ")))
        {
            Box->AddSlot().AutoHeight().Padding(0, 2)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.20f, 0.6f))
                .Padding(FMargin(10.0f, 4.0f, 4.0f, 4.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Line.Mid(2)))
                    .Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.8f)))
                    .AutoWrapText(true)
                ]
            ];
            continue;
        }

        // --- Bullet list: - item or * item or numbered: 1. item ---
        bool bIsBullet = false;
        FString BulletContent;
        if (Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("* ")))
        {
            bIsBullet = true;
            BulletContent = Line.Mid(2);
        }
        else
        {
            // Check for numbered list: "1. ", "2. ", etc.
            int32 DotPos = Line.Find(TEXT(". "));
            if (DotPos > 0 && DotPos <= 3)
            {
                bool bAllDigits = true;
                for (int32 i = 0; i < DotPos; ++i)
                {
                    if (!FChar::IsDigit(Line[i]))
                    {
                        bAllDigits = false;
                        break;
                    }
                }
                if (bAllDigits)
                {
                    bIsBullet = true;
                    // Keep the number prefix for numbered lists.
                    BulletContent = Line;
                }
            }
        }

        if (bIsBullet)
        {
            Box->AddSlot().AutoHeight().Padding(0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(4, 0, 6, 0)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("\x2022")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(BulletContent))
                    .AutoWrapText(true)
                ]
            ];
            continue;
        }

        // --- Horizontal rule: --- or *** or ___ ---
        FString Trimmed = Line.TrimStartAndEnd();
        if (Trimmed.Len() >= 3 &&
            (Trimmed.Replace(TEXT("-"), TEXT("")).IsEmpty() ||
             Trimmed.Replace(TEXT("*"), TEXT("")).IsEmpty() ||
             Trimmed.Replace(TEXT("_"), TEXT("")).IsEmpty()))
        {
            Box->AddSlot().AutoHeight().Padding(0, 4)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.3f, 0.3f, 0.3f, 0.5f))
                .Padding(FMargin(0, 0, 0, 1))
                [
                    SNew(SSpacer).Size(FVector2D(1, 1))
                ]
            ];
            continue;
        }

        // --- Empty line: add a small spacer ---
        if (Trimmed.IsEmpty())
        {
            Box->AddSlot().AutoHeight()
            [
                SNew(SSpacer).Size(FVector2D(1, 6))
            ];
            continue;
        }

        // --- Bold line: entire line wrapped in **..** ---
        if (Line.StartsWith(TEXT("**")) && Line.EndsWith(TEXT("**")) && Line.Len() > 4)
        {
            Box->AddSlot().AutoHeight().Padding(0, 1)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Line.Mid(2, Line.Len() - 4)))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                .AutoWrapText(true)
            ];
            continue;
        }

        // --- Default prose ---
        Box->AddSlot().AutoHeight().Padding(0, 1)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Line))
            .AutoWrapText(true)
        ];
    }

    return Box;
}

TSharedRef<SWidget> SUnrealAIChatPanel::CreateCodeBlockWidget(const FString& CodeText, const FString& Language)
{
    // Cap the visible height so very long T3D blocks don't take over the panel.
    const float MaxCodeHeight = 240.0f;

    // Header label: language + line count.
    int32 LineCount = 0;
    for (TCHAR Ch : CodeText)
    {
        if (Ch == TEXT('\n'))
        {
            ++LineCount;
        }
    }
    if (!CodeText.IsEmpty())
    {
        ++LineCount;
    }

    FString Header = Language.IsEmpty() ? TEXT("script") : Language;
    Header = FString::Printf(TEXT("%s  -  %d lines"), *Header, LineCount);

    // Capture the code text by value so the Copy lambda is self-contained.
    const FString CapturedCode = CodeText;

    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(FSlateColor(FLinearColor(0.05f, 0.05f, 0.07f, 1.0f)))
        .Padding(FMargin(4.0f))
        [
            SNew(SVerticalBox)

            // Header row: language pill + Copy button.
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2.0f, 0.0f, 2.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Header))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.5f)))
                    .TextStyle(FAppStyle::Get(), "SmallText")
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SSpacer)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("CopyCode", "Copy"))
                    .ToolTipText(LOCTEXT("CopyCodeTooltip", "Copy this block to the clipboard"))
                    .OnClicked_Lambda([CapturedCode]() -> FReply
                    {
                        FPlatformApplicationMisc::ClipboardCopy(*CapturedCode);
                        return FReply::Handled();
                    })
                ]
            ]

            // Body: scrollable monospace, read-only but selectable for manual copy.
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBox)
                .MaxDesiredHeight(MaxCodeHeight)
                [
                    SNew(SMultiLineEditableTextBox)
                    .Text(FText::FromString(CodeText))
                    .IsReadOnly(true)
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
                ]
            ]
        ];
}

#undef LOCTEXT_NAMESPACE

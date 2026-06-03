#include "UI/SUnrealAIControlPanel.h"

#include "UnrealAIBridge.h"
#include "UnrealAISettings.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "UnrealAIControlPanel"

namespace
{
    FString NormalizeKeyValue(const FString& Value)
    {
        return Value.TrimStartAndEnd().TrimQuotes();
    }

    FString GetOpenRouterApiKey(const FString& EnvFilePath)
    {
        FString FileContents;
        if (!FFileHelper::LoadFileToString(FileContents, *EnvFilePath))
        {
            return FString();
        }

        TArray<FString> Lines;
        FileContents.ParseIntoArrayLines(Lines);
        for (const FString& Line : Lines)
        {
            FString Trimmed = Line.TrimStartAndEnd();
            if (Trimmed.StartsWith(TEXT("OPENROUTER_API_KEY=")))
            {
                FString Value = Trimmed.RightChop(FCString::Strlen(TEXT("OPENROUTER_API_KEY=")));
                return NormalizeKeyValue(Value);
            }
        }

        return FString();
    }

    bool UpsertEnvValue(const FString& EnvFilePath, const FString& Key, const FString& Value, FString& OutError)
    {
        TArray<FString> Lines;
        FString ExistingContents;
        if (FPaths::FileExists(EnvFilePath) && FFileHelper::LoadFileToString(ExistingContents, *EnvFilePath))
        {
            ExistingContents.ParseIntoArrayLines(Lines);
        }

        const FString Prefix = Key + TEXT("=");
        bool bReplaced = false;
        for (FString& Line : Lines)
        {
            if (Line.TrimStart().StartsWith(Prefix))
            {
                Line = Prefix + Value;
                bReplaced = true;
                break;
            }
        }

        if (!bReplaced)
        {
            Lines.Add(Prefix + Value);
        }

        const FString Output = FString::Join(Lines, TEXT("\n")) + TEXT("\n");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(EnvFilePath), true);
        if (!FFileHelper::SaveStringToFile(Output, *EnvFilePath))
        {
            OutError = FString::Printf(TEXT("Failed to write %s"), *EnvFilePath);
            return false;
        }

        return true;
    }

    bool RemoveEnvValue(const FString& EnvFilePath, const FString& Key, FString& OutError)
    {
        FString ExistingContents;
        if (!FPaths::FileExists(EnvFilePath) || !FFileHelper::LoadFileToString(ExistingContents, *EnvFilePath))
        {
            return true;
        }

        TArray<FString> Lines;
        ExistingContents.ParseIntoArrayLines(Lines);

        const FString Prefix = Key + TEXT("=");
        Lines.RemoveAll([&Prefix](const FString& Line)
        {
            return Line.TrimStart().StartsWith(Prefix);
        });

        const FString Output = Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) + TEXT("\n") : FString();
        if (!FFileHelper::SaveStringToFile(Output, *EnvFilePath))
        {
            OutError = FString::Printf(TEXT("Failed to update %s"), *EnvFilePath);
            return false;
        }

        return true;
    }

    bool IsTcpPortReachable(const FString& Host, uint16 Port)
    {
        ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!SocketSubsystem)
        {
            return false;
        }

        FIPv4Address Address;
        if (!FIPv4Address::Parse(Host, Address))
        {
            return false;
        }

        TSharedRef<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();
        InternetAddr->SetIp(Address.Value);
        InternetAddr->SetPort(Port);

        FSocket* ProbeSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealAIControlPanelProbe"), false);
        if (!ProbeSocket)
        {
            return false;
        }

        ProbeSocket->SetNonBlocking(false);
        ProbeSocket->SetReuseAddr(true);

        const bool bConnected = ProbeSocket->Connect(*InternetAddr);
        SocketSubsystem->DestroySocket(ProbeSocket);
        return bConnected;
    }

    void EnsureCopilotAgentFile(const FString& AgentPath)
    {
        if (FPaths::FileExists(AgentPath))
        {
            return;
        }

        const FString AgentBody = TEXT(
            "---\n"
            "name: unreal-mcp\n"
            "description: \"Drives the open Unreal Editor through the UnrealAI MCP server. Use for actor spawning, inspection, blueprint editing, and level operations.\"\n"
            "model: claude-sonnet-4.5\n"
            "---\n"
            "\n"
            "You are an Unreal Engine assistant connected to a live editor session through the unrealai MCP server.\n"
            "Call ping at the start of a session if the user might not have the bridge open.\n"
            "Prefer exposed UnrealAI tools over alternate scripts or commandlets.\n"
            "When unsure whether a capability exists, call list_unreal_tools first and use find_assets for asset discovery instead of guessing paths.\n"
            "Only fall back to another approach after the tool is confirmed absent or the tool call fails.\n"
            "For map changes, use new_blank_map or open_level instead of inventing editor scripting workarounds.\n"
            "Inspect before mutating and compile after Blueprint edits.\n");
        FFileHelper::SaveStringToFile(AgentBody, *AgentPath);
    }
}

void SUnrealAIControlPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(8.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ControlPanelTitle", "UnrealAI Control"))
                .Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ControlPanelSubtitle", "Use this panel for MCP bridge status, VS Code launch, and local API key management."))
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f)
            [
                SNew(SSeparator)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ConnectionHeader", "Connection"))
                .Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 2.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("BridgeLabel", "MCP bridge:"))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(BridgeStatusText, STextBlock)
                    .Text(LOCTEXT("BridgeChecking", "Checking..."))
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 2.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("AgentLabel", "Legacy chat agent:"))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(AgentStatusText, STextBlock)
                    .Text(LOCTEXT("AgentChecking", "Checking..."))
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 2.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ApiStatusLabel", "OpenRouter key:"))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(ApiKeyStatusText, STextBlock)
                    .Text(LOCTEXT("ApiStatusChecking", "Checking..."))
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("RefreshButton", "Refresh Status"))
                    .OnClicked(this, &SUnrealAIControlPanel::OnRefreshStatusClicked)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("OpenVSCodeButton", "Open in VS Code"))
                    .OnClicked(this, &SUnrealAIControlPanel::OnOpenInVSCodeClicked)
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 12.0f, 0.0f, 6.0f)
            [
                SNew(SSeparator)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ApiHeader", "Own API Key"))
                .Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ApiHelp", "This stores OPENROUTER_API_KEY in the configured local agent env file. It is not written into project settings."))
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
            [
                SAssignNew(ApiKeyTextBox, SEditableTextBox)
                .HintText(LOCTEXT("ApiKeyHint", "Paste OpenRouter API key"))
                .IsPassword(true)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("SaveApiButton", "Save API Key"))
                    .OnClicked(this, &SUnrealAIControlPanel::OnSaveApiKeyClicked)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ClearApiButton", "Clear API Key"))
                    .OnClicked(this, &SUnrealAIControlPanel::OnClearApiKeyClicked)
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(PanelStatusText, STextBlock)
                .Text(LOCTEXT("PanelReady", "Ready"))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)))
                .AutoWrapText(true)
            ]
        ]
    ];

    RefreshStatus();
}

FReply SUnrealAIControlPanel::OnRefreshStatusClicked()
{
    RefreshStatus();
    SetPanelStatus(TEXT("Refreshed UnrealAI status."), FLinearColor(0.6f, 0.8f, 0.6f));
    return FReply::Handled();
}

FReply SUnrealAIControlPanel::OnOpenInVSCodeClicked()
{
    const UUnrealAISettings* Settings = GetDefault<UUnrealAISettings>();
    const FString PythonExe = Settings->PythonExecutablePath.TrimStartAndEnd();
    const FString McpScript = Settings->McpServerScriptPath.TrimStartAndEnd();
    if (PythonExe.IsEmpty() || McpScript.IsEmpty())
    {
        SetPanelStatus(TEXT("Set PythonExecutablePath and McpServerScriptPath in Project Settings -> Plugins -> UnrealAI first."), FLinearColor(1.0f, 0.4f, 0.4f));
        return FReply::Handled();
    }

    const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

    const FString VsCodeDir = FPaths::Combine(ProjectDir, TEXT(".vscode"));
    const FString McpJsonPath = FPaths::Combine(VsCodeDir, TEXT("mcp.json"));
    IFileManager::Get().MakeDirectory(*VsCodeDir, true);

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

    if (!FFileHelper::SaveStringToFile(McpJson, *McpJsonPath))
    {
        SetPanelStatus(FString::Printf(TEXT("Failed to write %s"), *McpJsonPath), FLinearColor(1.0f, 0.4f, 0.4f));
        return FReply::Handled();
    }

    const FString AgentDir = FPaths::Combine(ProjectDir, TEXT(".claude"), TEXT("agents"));
    const FString AgentPath = FPaths::Combine(AgentDir, TEXT("unreal-mcp.md"));
    IFileManager::Get().MakeDirectory(*AgentDir, true);
    EnsureCopilotAgentFile(AgentPath);

    const FString CmdExe = TEXT("cmd.exe");
    const FString CmdArgs = FString::Printf(TEXT("/c code \"%s\" --new-window"), *ProjectDir);
    FProcHandle Handle = FPlatformProcess::CreateProc(
        *CmdExe,
        *CmdArgs,
        true,
        true,
        true,
        nullptr,
        0,
        nullptr,
        nullptr);

    if (!Handle.IsValid())
    {
        SetPanelStatus(TEXT("Failed to launch VS Code. Make sure the 'code' command is available on PATH."), FLinearColor(1.0f, 0.4f, 0.4f));
        return FReply::Handled();
    }

    SetPanelStatus(TEXT("Opened the current Unreal project in a new VS Code window and refreshed .vscode/mcp.json."), FLinearColor(0.6f, 0.8f, 0.6f));
    return FReply::Handled();
}

FReply SUnrealAIControlPanel::OnSaveApiKeyClicked()
{
    if (!ApiKeyTextBox.IsValid())
    {
        return FReply::Handled();
    }

    const FString ApiKey = ApiKeyTextBox->GetText().ToString().TrimStartAndEnd();
    if (ApiKey.IsEmpty())
    {
        SetPanelStatus(TEXT("Paste an API key before saving."), FLinearColor(1.0f, 0.7f, 0.0f));
        return FReply::Handled();
    }

    const UUnrealAISettings* Settings = GetDefault<UUnrealAISettings>();
    FString Error;
    if (!UpsertEnvValue(Settings->AgentEnvFilePath.TrimStartAndEnd(), TEXT("OPENROUTER_API_KEY"), ApiKey, Error))
    {
        SetPanelStatus(Error, FLinearColor(1.0f, 0.4f, 0.4f));
        return FReply::Handled();
    }

    ApiKeyTextBox->SetText(FText::GetEmpty());
    RefreshApiKeyStatus();
    SetPanelStatus(TEXT("Saved OPENROUTER_API_KEY to the configured local env file. Restart the local agent if it is already running."), FLinearColor(0.6f, 0.8f, 0.6f));
    return FReply::Handled();
}

FReply SUnrealAIControlPanel::OnClearApiKeyClicked()
{
    const UUnrealAISettings* Settings = GetDefault<UUnrealAISettings>();
    FString Error;
    if (!RemoveEnvValue(Settings->AgentEnvFilePath.TrimStartAndEnd(), TEXT("OPENROUTER_API_KEY"), Error))
    {
        SetPanelStatus(Error, FLinearColor(1.0f, 0.4f, 0.4f));
        return FReply::Handled();
    }

    if (ApiKeyTextBox.IsValid())
    {
        ApiKeyTextBox->SetText(FText::GetEmpty());
    }

    RefreshApiKeyStatus();
    SetPanelStatus(TEXT("Cleared OPENROUTER_API_KEY from the configured local env file."), FLinearColor(0.6f, 0.8f, 0.6f));
    return FReply::Handled();
}

void SUnrealAIControlPanel::RefreshStatus()
{
    RefreshBridgeStatus();
    RefreshApiKeyStatus();
    RefreshAgentStatus();
}

void SUnrealAIControlPanel::RefreshBridgeStatus()
{
    if (!BridgeStatusText.IsValid())
    {
        return;
    }

    const UUnrealAIBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UUnrealAIBridge>() : nullptr;
    const bool bSubsystemRunning = Bridge && Bridge->IsRunning();
    const bool bPortReachable = IsTcpPortReachable(TEXT("127.0.0.1"), 55557);
    if (bSubsystemRunning && bPortReachable)
    {
        BridgeStatusText->SetText(FText::FromString(TEXT("Reachable on 127.0.0.1:55557")));
        BridgeStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)));
    }
    else if (bSubsystemRunning)
    {
        BridgeStatusText->SetText(FText::FromString(TEXT("Started in editor, but TCP port is not reachable yet")));
        BridgeStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.7f, 0.0f)));
    }
    else
    {
        BridgeStatusText->SetText(FText::FromString(TEXT("Not reachable")));
        BridgeStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.4f)));
    }
}

void SUnrealAIControlPanel::RefreshApiKeyStatus()
{
    if (!ApiKeyStatusText.IsValid())
    {
        return;
    }

    const UUnrealAISettings* Settings = GetDefault<UUnrealAISettings>();
    const FString EnvFilePath = Settings->AgentEnvFilePath.TrimStartAndEnd();
    const FString ApiKey = GetOpenRouterApiKey(EnvFilePath);
    if (ApiKey.IsEmpty())
    {
        ApiKeyStatusText->SetText(FText::FromString(FString::Printf(TEXT("Not configured (%s)"), *EnvFilePath)));
        ApiKeyStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.7f, 0.0f)));
    }
    else
    {
        ApiKeyStatusText->SetText(FText::FromString(FString::Printf(TEXT("Configured in %s"), *EnvFilePath)));
        ApiKeyStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)));
    }
}

void SUnrealAIControlPanel::RefreshAgentStatus()
{
    if (!AgentStatusText.IsValid())
    {
        return;
    }

    const UUnrealAISettings* Settings = GetDefault<UUnrealAISettings>();
    const FString AgentBaseUrl = Settings->AgentBaseUrl.TrimStartAndEnd();
    if (AgentBaseUrl.IsEmpty())
    {
        AgentStatusText->SetText(FText::FromString(TEXT("Not configured")));
        AgentStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.7f, 0.0f)));
        return;
    }

    AgentStatusText->SetText(FText::FromString(FString::Printf(TEXT("Checking %s/health ..."), *AgentBaseUrl)));
    AgentStatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(AgentBaseUrl + TEXT("/health"));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(3.0f);
    Request->OnProcessRequestComplete().BindSP(this, &SUnrealAIControlPanel::OnAgentHealthResponse);
    Request->ProcessRequest();
}

void SUnrealAIControlPanel::OnAgentHealthResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
    if (!AgentStatusText.IsValid())
    {
        return;
    }

    if (!bSucceeded || !Response.IsValid())
    {
        AgentStatusText->SetText(FText::FromString(TEXT("Unreachable (optional)")));
        AgentStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.4f)));
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    if (ResponseCode >= 200 && ResponseCode < 300)
    {
        AgentStatusText->SetText(FText::FromString(TEXT("Reachable")));
        AgentStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)));
    }
    else
    {
        AgentStatusText->SetText(FText::FromString(FString::Printf(TEXT("HTTP %d"), ResponseCode)));
        AgentStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.7f, 0.0f)));
    }
}

void SUnrealAIControlPanel::SetPanelStatus(const FString& Message, const FLinearColor& Color)
{
    if (!PanelStatusText.IsValid())
    {
        return;
    }

    PanelStatusText->SetText(FText::FromString(Message));
    PanelStatusText->SetColorAndOpacity(FSlateColor(Color));
}

#undef LOCTEXT_NAMESPACE
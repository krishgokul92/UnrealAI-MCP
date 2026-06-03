#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Interfaces/IHttpRequest.h"

class SEditableTextBox;
class STextBlock;

class UNREALAI_API SUnrealAIControlPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SUnrealAIControlPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FReply OnRefreshStatusClicked();
    FReply OnOpenInVSCodeClicked();
    FReply OnSaveApiKeyClicked();
    FReply OnClearApiKeyClicked();

    void RefreshStatus();
    void RefreshBridgeStatus();
    void RefreshApiKeyStatus();
    void RefreshAgentStatus();
    void OnAgentHealthResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);

    void SetPanelStatus(const FString& Message, const FLinearColor& Color);

private:
    TSharedPtr<STextBlock> BridgeStatusText;
    TSharedPtr<STextBlock> AgentStatusText;
    TSharedPtr<STextBlock> ApiKeyStatusText;
    TSharedPtr<STextBlock> PanelStatusText;
    TSharedPtr<SEditableTextBox> ApiKeyTextBox;
};
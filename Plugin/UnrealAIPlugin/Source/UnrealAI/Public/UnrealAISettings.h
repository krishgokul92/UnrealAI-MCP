#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "UnrealAISettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="UnrealAI"))
class UNREALAI_API UUnrealAISettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UUnrealAISettings();

    virtual FName GetCategoryName() const override;
    virtual FName GetSectionName() const override;

    UPROPERTY(Config, EditAnywhere, Category="MCP")
    FString PythonExecutablePath;

    UPROPERTY(Config, EditAnywhere, Category="MCP")
    FString McpServerScriptPath;

    UPROPERTY(Config, EditAnywhere, Category="Agent")
    FString AgentBaseUrl;

    UPROPERTY(Config, EditAnywhere, Category="Agent")
    FString AgentEnvFilePath;
};
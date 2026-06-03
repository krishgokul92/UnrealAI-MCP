#include "UnrealAISettings.h"

UUnrealAISettings::UUnrealAISettings()
{
    PythonExecutablePath = TEXT("e:/unrealBP/.venv/Scripts/python.exe");
    McpServerScriptPath = TEXT("e:/unrealBP/UnrealAI/Python/unreal_ai_mcp.py");
    AgentBaseUrl = TEXT("http://127.0.0.1:8765");
    AgentEnvFilePath = TEXT("e:/unrealBP/UnrealAI/Python/.env");
}

FName UUnrealAISettings::GetCategoryName() const
{
    return TEXT("Plugins");
}

FName UUnrealAISettings::GetSectionName() const
{
    return TEXT("UnrealAI");
}
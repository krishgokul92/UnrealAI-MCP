#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FSpawnTabArgs;
class SDockTab;

class FUnrealAIModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static inline FUnrealAIModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FUnrealAIModule>("UnrealAI");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("UnrealAI");
	}

	/** Identifier for the control panel tab. */
	static const FName ControlPanelTabId;

private:
	/** Spawn the UnrealAI control panel tab. */
	TSharedRef<SDockTab> OnSpawnControlPanelTab(const FSpawnTabArgs& SpawnTabArgs);

	/** Register the Window menu entry and tab spawner. */
	void RegisterMenus();
};

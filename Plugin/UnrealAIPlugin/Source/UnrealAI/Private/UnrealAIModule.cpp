#include "UnrealAIModule.h"
#include "UnrealAIBridge.h"
#include "UI/SUnrealAIControlPanel.h"

#include "Modules/ModuleManager.h"
#include "EditorSubsystem.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "FUnrealAIModule"

const FName FUnrealAIModule::ControlPanelTabId = FName(TEXT("UnrealAIControlPanel"));

void FUnrealAIModule::StartupModule()
{
	UE_LOG(LogTemp, Display, TEXT("UnrealAI Module has started"));

	// Defer menu registration until after the editor is fully initialized,
	// so the Window menu and workspace categories are available.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealAIModule::RegisterMenus));
}

void FUnrealAIModule::ShutdownModule()
{
	UE_LOG(LogTemp, Display, TEXT("UnrealAI Module has shut down"));

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ControlPanelTabId);
	}
}

void FUnrealAIModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Register the tab spawner so the tab can be opened/docked.
	const TSharedRef<FWorkspaceItem> Category = WorkspaceMenu::GetMenuStructure().GetToolsCategory();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ControlPanelTabId,
		FOnSpawnTab::CreateRaw(this, &FUnrealAIModule::OnSpawnControlPanelTab))
		.SetDisplayName(LOCTEXT("ControlPanelTabTitle", "UnrealAI"))
		.SetTooltipText(LOCTEXT("ControlPanelTabTooltip", "Open the UnrealAI control panel"))
		.SetGroup(Category)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	// Add a menu entry to the Window menu.
	UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	if (WindowMenu)
	{
		FToolMenuSection& Section = WindowMenu->FindOrAddSection("WindowLayout");
		Section.AddMenuEntry(
			"OpenUnrealAIControlPanel",
			LOCTEXT("OpenUnrealAILabel", "UnrealAI"),
			LOCTEXT("OpenUnrealAITooltip", "Open the UnrealAI control panel"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(FUnrealAIModule::ControlPanelTabId);
			}))
		);
	}
}

TSharedRef<SDockTab> FUnrealAIModule::OnSpawnControlPanelTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("ControlPanelTabTitle", "UnrealAI"))
		[
			SNew(SUnrealAIControlPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUnrealAIModule, UnrealAI)

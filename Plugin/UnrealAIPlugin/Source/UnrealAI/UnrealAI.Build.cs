// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealAI : ModuleRules
{
	public UnrealAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDefinitions.Add("UNREALAI_EXPORTS=1");

		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "Public"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Commands"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Commands/BlueprintGraph"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Commands/BlueprintGraph/Nodes"),
				System.IO.Path.Combine(ModuleDirectory, "Public/UI")
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "Private"),
				System.IO.Path.Combine(ModuleDirectory, "Private/Commands"),
				System.IO.Path.Combine(ModuleDirectory, "Private/Commands/BlueprintGraph"),
				System.IO.Path.Combine(ModuleDirectory, "Private/Commands/BlueprintGraph/Nodes"),
				System.IO.Path.Combine(ModuleDirectory, "Private/UI")
			}
		);
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Networking",
				"Sockets",
				"HTTP",
				"Json",
				"JsonUtilities",
				"DeveloperSettings",
				"PhysicsCore",
				"UMG",
				"UnrealEd",           // For Blueprint editing
				"BlueprintGraph",     // For K2Node classes (F15-F22)
				"KismetCompiler"      // For Blueprint compilation (F15-F22)
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AIGraph",
				"AIModule",
				"AnimGraph",
				"BehaviorTreeEditor",
				"ComputeFramework",
				"EditorScriptingUtilities",
				"EditorSubsystem",
				"Foliage",
				"GameplayTasks",
				"LevelSequence",
				"LevelSequenceEditor",
				"MaterialEditor",
				"MovieScene",
				"MovieSceneTracks",
				"Niagara",
				"NiagaraCore",
				"NiagaraEditor",
				"PCG",
				"PCGEditor",
				"UMGEditor",
				"Slate",
				"SlateCore",
				"ApplicationCore",
				"Kismet",
				"Projects",
				"AssetRegistry",
				"AssetTools",
				"Landscape",
				"LandscapeEditor"
			}
		);
		
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"PropertyEditor",           // For property editing
					"ToolMenus",                // For editor UI
					"BlueprintEditorLibrary",   // For Blueprint utilities
					"WorkspaceMenuStructure",   // For Window menu tab registration
					"InputCore",                // For key bindings in input box
					"EditorStyle",              // For FEditorStyle in UI
					"MainFrame"                 // For main editor frame access
				}
			);
		}
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
} 
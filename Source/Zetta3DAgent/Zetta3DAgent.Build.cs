// Copyright Zetta 3D. See LICENSE.md.

using UnrealBuildTool;

public class Zetta3DAgent : ModuleRules
{
	public Zetta3DAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "Slate", "SlateCore", "InputCore", "Json", "Zetta3DEditor",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"UnrealEd", "EditorFramework", "ToolMenus", "Projects", "ApplicationCore", "HTTP", "JsonUtilities",
			"AssetTools", "AssetRegistry", "ContentBrowser", "ContentBrowserData", "LevelEditor", "SceneOutliner",
			"Kismet", "KismetCompiler", "BlueprintGraph", "MaterialEditor", "PythonScriptPlugin", "ImageWrapper",
			"ImageCore", "RenderCore", "RHI", "DesktopPlatform", "EditorSubsystem", "EditorScriptingUtilities",
			"DeveloperSettings", "Settings", "SourceCodeAccess", "EngineSettings", "EnhancedInput",
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Live Coding builds the project's C++ for the agent's `compile` tool.
			PrivateDependencyModuleNames.Add("LiveCoding");
		}
	}
}

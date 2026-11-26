// Copyright Zetta 3D. See LICENSE.md.

using UnrealBuildTool;

public class Zetta3DEditor : ModuleRules
{
	public Zetta3DEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "Slate", "SlateCore", "InputCore",
			"HTTP", "Json", "JsonUtilities", "DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"UnrealEd", "EditorFramework", "ToolMenus", "Projects", "ApplicationCore",
			"Sockets", "Networking", "ImageWrapper", "ImageCore", "AssetTools", "AssetRegistry",
			"ContentBrowser", "LevelEditor", "DesktopPlatform", "EditorSubsystem", "WorkspaceMenuStructure",
			"MainFrame", "Settings", "RenderCore", "EditorWidgets", "PropertyEditor",
		});

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// The session token lives in the login keychain, never in the project or the editor's ini files.
			PublicFrameworks.Add("Security");
			PublicFrameworks.Add("CoreFoundation");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Windows Credential Manager (CredReadW / CredWriteW / CredDeleteW).
			PublicSystemLibraries.Add("Advapi32.lib");
		}
	}
}

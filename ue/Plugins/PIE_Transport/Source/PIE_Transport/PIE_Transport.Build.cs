using UnrealBuildTool;

public class PIE_Transport : ModuleRules
{
	public PIE_Transport(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"EditorScriptingUtilities",
				"EditorStyle",
				"EnhancedInput",
				"InputCore",
				"Kismet",
				"LevelEditor",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UE_MCP_Bridge",
				"UnrealEd",
				"WorkspaceMenuStructure",
			}
		);
	}
}

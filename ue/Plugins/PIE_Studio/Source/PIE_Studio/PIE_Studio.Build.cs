using UnrealBuildTool;

public class PIE_Studio : ModuleRules
{
	public PIE_Studio(ReadOnlyTargetRules Target) : base(Target)
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
				"ImageWrapper",
				"InputCore",
				"Kismet",
				"LevelEditor",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UE_MCP_Bridge",
				"PIE_StudioRuntime",
				"UnrealEd",
				"WorkspaceMenuStructure",
			}
		);

		// UAT scenarios validate their regex patterns with ICU, the engine's regex backend.
		if (Target.bCompileICU)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "ICU");
		}
	}
}

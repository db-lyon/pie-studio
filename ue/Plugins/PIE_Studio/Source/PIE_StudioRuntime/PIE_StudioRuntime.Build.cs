using UnrealBuildTool;

// Runs in every process a PIE session uses, including separate-process clients (`-game`), which load no Editor
// modules. DeveloperTool keeps it out of shipping builds.
public class PIE_StudioRuntime : ModuleRules
{
	public PIE_StudioRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EnhancedInput",
			"InputCore",
			"RenderCore",
			"RHI",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper",
		});
	}
}

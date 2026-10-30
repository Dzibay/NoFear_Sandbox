using UnrealBuildTool;

public class NoFear : ModuleRules
{
	public NoFear(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"GameplayTasks",
			"NavigationSystem",
			"EnhancedInput",
			"Niagara",
			"PhysicsCore",
			"RenderCore"
		});
	}
}

using UnrealBuildTool;

public class NoFearEditor : ModuleRules
{
	public NoFearEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"AIGraph",
			"AIModule",
			"AssetRegistry",
			"BehaviorTreeEditor",
			"GameplayTasks",
			"NoFear",
			"UnrealEd"
		});
	}
}

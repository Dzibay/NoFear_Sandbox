using UnrealBuildTool;
using System.Collections.Generic;

public class NoFear_Sandbox_UE_ProjectTarget : TargetRules
{
	public NoFear_Sandbox_UE_ProjectTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("NoFear");
	}
}

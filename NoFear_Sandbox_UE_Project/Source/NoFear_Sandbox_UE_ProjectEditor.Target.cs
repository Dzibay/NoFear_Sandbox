using UnrealBuildTool;
using System.Collections.Generic;

public class NoFear_Sandbox_UE_ProjectEditorTarget : TargetRules
{
	public NoFear_Sandbox_UE_ProjectEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("NoFear");
	}
}

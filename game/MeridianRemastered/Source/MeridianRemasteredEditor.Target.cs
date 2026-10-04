using UnrealBuildTool;

public class MeridianRemasteredEditorTarget : TargetRules
{
	public MeridianRemasteredEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MeridianRemastered");
	}
}

using UnrealBuildTool;

public class UnrealMeridianEditorTarget : TargetRules
{
	public UnrealMeridianEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("UnrealMeridian");
	}
}

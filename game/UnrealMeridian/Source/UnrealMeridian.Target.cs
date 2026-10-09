using UnrealBuildTool;

public class UnrealMeridianTarget : TargetRules
{
	public UnrealMeridianTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("UnrealMeridian");
	}
}

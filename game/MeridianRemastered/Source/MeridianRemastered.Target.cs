using UnrealBuildTool;

public class MeridianRemasteredTarget : TargetRules
{
	public MeridianRemasteredTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MeridianRemastered");
	}
}

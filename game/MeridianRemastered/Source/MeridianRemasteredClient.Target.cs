using UnrealBuildTool;

// Client-only build (no server code). Like the server target, it needs a source build of the engine.
public class MeridianRemasteredClientTarget : TargetRules
{
	public MeridianRemasteredClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MeridianRemastered");
	}
}

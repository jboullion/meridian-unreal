using UnrealBuildTool;

// Dedicated server. Building this target needs a source build of the engine (Phase 8);
// during development use Play-In-Editor with "Play As Client" (runs a dedicated server in-process).
public class MeridianRemasteredServerTarget : TargetRules
{
	public MeridianRemasteredServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MeridianRemastered");
	}
}

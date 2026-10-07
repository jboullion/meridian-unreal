using UnrealBuildTool;

public class MeridianRemastered : ModuleRules
{
	public MeridianRemastered(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"NetCore",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore",
			"RHI",
			"RenderCore",
			"AudioMixer",
			"WebSockets",
			"HTTP"
		});

		PublicIncludePaths.Add(ModuleDirectory);

		SetupIrisSupport(Target);
	}
}

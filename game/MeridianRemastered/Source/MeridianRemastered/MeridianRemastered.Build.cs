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
			"HTTP",
			"ProceduralMeshComponent",
			"PhysicsCore",
			// the server's sounds, decoded at runtime (Audio/MRServerSound)
			"VorbisAudioDecoder",
			"AudioExtensions"
		});

		// FSoundQualityInfo (the decoder's interface) is declared there; header only, as the Engine module uses it
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		PublicIncludePaths.Add(ModuleDirectory);

		SetupIrisSupport(Target);
	}
}

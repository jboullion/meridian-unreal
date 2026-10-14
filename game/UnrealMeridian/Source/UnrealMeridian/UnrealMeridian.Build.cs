using UnrealBuildTool;

public class UnrealMeridian : ModuleRules
{
	public UnrealMeridian(ReadOnlyTargetRules Target) : base(Target)
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
			"AudioExtensions",
			// the release's version on the login screen (UGeneralProjectSettings::ProjectVersion)
			"EngineSettings",
			// the warm-up lists the interface's art and icons to preload (Core/MRWarmup)
			"AssetRegistry"
		});

		// FSoundQualityInfo (the decoder's interface) is declared there; header only, as the Engine module uses it
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		PublicIncludePaths.Add(ModuleDirectory);

		SetupIrisSupport(Target);
	}
}

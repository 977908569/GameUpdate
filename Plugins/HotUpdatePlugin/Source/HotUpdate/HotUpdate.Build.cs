// Copyright czm. All Rights Reserved.

using UnrealBuildTool;

public class HotUpdate : ModuleRules
{
	public HotUpdate(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bLegacyPublicIncludePaths = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"PakFile",
			"Json",
			"JsonUtilities",
			"DeveloperSettings",
			"AssetRegistry"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"ApplicationCore",
			"HTTP"
		});

		// Android 平台依赖
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.Add("AndroidRuntimeSettings");
		}
	}
}
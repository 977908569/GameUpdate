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
			"PakFile",
			"Json",
			"JsonUtilities",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"HTTP",
			"AssetRegistry"
		});

			}
}
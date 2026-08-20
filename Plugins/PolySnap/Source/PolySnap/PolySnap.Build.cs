// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

using UnrealBuildTool;

public class PolySnap : ModuleRules
{
	public PolySnap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Chaos",
			"EnhancedInput",
			"InputCore",
			"PhysicsCore",
		});
	}
}

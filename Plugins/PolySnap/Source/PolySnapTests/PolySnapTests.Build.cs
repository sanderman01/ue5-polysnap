// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

using UnrealBuildTool;

public class PolySnapTests : ModuleRules
{
	public PolySnapTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PolySnap",
		});
	}
}

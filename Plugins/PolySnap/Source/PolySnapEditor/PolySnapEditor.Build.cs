// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

using UnrealBuildTool;

public class PolySnapEditor : ModuleRules
{
	public PolySnapEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PolySnap",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"MeshDescription",
			"StaticMeshDescription",
		});
	}
}

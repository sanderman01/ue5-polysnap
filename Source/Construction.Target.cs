// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ConstructionTarget : TargetRules
{
	public ConstructionTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;

		ExtraModuleNames.AddRange( new string[] { "Construction" } );
	}
}

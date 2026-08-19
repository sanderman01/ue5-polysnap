// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class PolySnapSandboxTarget : TargetRules
{
	public PolySnapSandboxTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange( new string[] { "Sandbox" } );
	}
}

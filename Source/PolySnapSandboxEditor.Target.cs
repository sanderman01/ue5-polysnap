// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class PolySnapSandboxEditorTarget : TargetRules
{
	public PolySnapSandboxEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange( new string[] { "Sandbox" } );
	}
}

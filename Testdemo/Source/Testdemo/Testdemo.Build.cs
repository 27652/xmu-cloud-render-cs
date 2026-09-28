// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Testdemo : ModuleRules
{
	public Testdemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay" });
	}
}

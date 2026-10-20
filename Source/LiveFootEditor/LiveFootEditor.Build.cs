using UnrealBuildTool;

public class LiveFootEditor : ModuleRules
{
	public LiveFootEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LiveFoot" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore", "UnrealEd", "ToolMenus", "ContentBrowser", "AssetTools", "Projects",
			"AssetRegistry", "AnimationEditor", "AnimGraph", "Persona", "InputCore"
		});
	}
}

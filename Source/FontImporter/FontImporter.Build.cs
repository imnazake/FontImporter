using UnrealBuildTool;

public class FontImporter : ModuleRules
{
    public FontImporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "AssetRegistry",
            "AssetTools",
            "ContentBrowser",
            "DesktopPlatform",
            "ToolMenus"
        });
    }
}

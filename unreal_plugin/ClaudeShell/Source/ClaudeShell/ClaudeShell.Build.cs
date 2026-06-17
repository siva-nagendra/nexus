using UnrealBuildTool;

public class ClaudeShell : ModuleRules
{
    public ClaudeShell(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "UnrealEd",
            "LevelEditor",
            "ToolMenus",
            "WebBrowser",
            "Projects",
            "Sockets",
            "Networking",
            "HTTP",
            "Json",
            "JsonUtilities"
        });
    }
}

#include <memory>
#include <iostream>
#include <string_view>

#include "Core.h"
#include "Configuration/EngineConfigLoader.h"
#include "ImGui/ImGuiLayer.h"

int main(int argc, char** argv)
{
    // Same config file as the Game (Assets/Config/EngineConfig.json), plus its "editor" overrides.
    auto config = Crucible::EngineConfigLoader::LoadForApp(Crucible::AppKind::Editor);
    // Open a scene directly without changing the team's shared startup configuration.
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--scene") {
            std::cerr << "Usage: Editor [--scene path-relative-to-Assets]\n";
            return 1;
        }
        config.project.startupScene = argv[2];
    }
    Crucible::Core core(config);

    if (!core.Init())
    {
        return 1;
    }

    // The editor is the engine + an ImGui layer on top of it.
    // Panels (e.g. the Inspector's mesh/material pickers) reach the ResourceManager through
    // EditorManager - see EditorManager::GetResources().
    Crucible::EditorManager::GetInstance().SetResources(&core.GetResources());
    core.PushLayer(std::make_unique<Crucible::ImGuiLayer>(core));

    core.Run();
    return 0;
}

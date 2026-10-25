#include <memory>

#include "Core.h"
#include "Configuration/EngineConfigLoader.h"
#include "Diagnostics/Logger.h"
#include "ImGui/ImGuiLayer.h"

int main()
{
    // Starts the log, and ends it when main returns: the closing banner, a final flush
    // and the file closed properly. Declared before Core so it is destroyed AFTER it, and
    // the engine's own shutdown messages still reach the file. Without it the logger
    // starts itself on the first message and is never shut down.
    Crucible::Debug::LogSession logSession{};

    // Same config file as the Game (Assets/Config/EngineConfig.json), plus its "editor" overrides.
    Crucible::Core core(Crucible::EngineConfigLoader::LoadForApp(Crucible::AppKind::Editor));

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

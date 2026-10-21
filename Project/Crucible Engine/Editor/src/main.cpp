#include <exception>
#include <memory>

#include "Core.h"
#include "Configuration/EngineConfigLoader.h"
#include "AssetPipe/AssetPaths.h"
#include "Diagnostics/FatalError.h"
#include "ImGui/ImGuiLayer.h"

int main()
{
    // Before anything else: without the Assets folder there is no config, no shaders and no
    // scenes. This says what was searched, instead of failing somewhere inside the engine.
    if (!Crucible::RequireAssetRoot())
    {
        return 1;
    }

    // Everything inside the try, so a failure anywhere (no Vulkan GPU, a broken shader, ...)
    // ends in a message box saying why, not an abort() dialog.
    try
    {
        // Same config file as the Game (Assets/Config/EngineConfig.json), plus its "editor" overrides.
        Crucible::Core core(Crucible::EngineConfigLoader::LoadForApp(Crucible::AppKind::Editor));

        if (!core.Init())
        {
            Crucible::ShowFatalError("Crucible Editor", "The editor could not start.");
            return 1;
        }

        // The editor is the engine + an ImGui layer on top of it.
        // Panels (e.g. the Inspector's mesh/material pickers) reach the ResourceManager through
        // EditorManager - see EditorManager::GetResources().
        Crucible::EditorManager::GetInstance().SetResources(&core.GetResources());
        core.PushLayer(std::make_unique<Crucible::ImGuiLayer>(core));

        core.Run();
    }
    catch (const std::exception& e)
    {
        Crucible::ShowFatalError("Crucible Editor", e.what());
        return 1;
    }
    catch (...)
    {
        Crucible::ShowFatalError("Crucible Editor", "An unknown error stopped the editor.");
        return 1;
    }
    return 0;
}

#include <memory>

#include "Core.h"
#include "ImGui/ImGuiLayer.h"

int main()
{
    Crucible::Core core;

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

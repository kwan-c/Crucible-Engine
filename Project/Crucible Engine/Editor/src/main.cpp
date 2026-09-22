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
    core.PushLayer(std::make_unique<Crucible::ImGuiLayer>(core.GetRenderer()));

    core.Run();
    return 0;
}

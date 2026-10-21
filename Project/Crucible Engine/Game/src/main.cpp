#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#include <exception>

#include "Application/GameApp.h"
#include "Diagnostics/Logger.h"
#include "Diagnostics/FatalError.h"
#include "AssetPipe/AssetPaths.h"

int main()
{
    // Enable memory leak checking at program exit
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    // Before anything else: without the Assets folder there is no config, no shaders and no
    // scenes. This tells the player what was searched, instead of failing somewhere inside.
    if (!Crucible::RequireAssetRoot())
    {
        return EXIT_FAILURE;
    }

    // Everything - building the engine, Init and the game loop - inside the try, so a failure
    // anywhere (no Vulkan GPU, a broken shader, ...) is shown to the player in a message box.
    // Release builds have no console, so std::cerr alone would go nowhere.
    try
    {
        GameApp game;

        if (!game.Init())
        {
            Crucible::ShowFatalError("Mekanomachia", "The game could not start.");
            return EXIT_FAILURE;
        }

        game.Run();
    }
    catch (const std::exception& e)
    {
        Crucible::ShowFatalError("Mekanomachia", e.what());
        return EXIT_FAILURE;
    }
    catch (...)
    {
        Crucible::ShowFatalError("Mekanomachia", "An unknown error stopped the game.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

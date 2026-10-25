#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#include <stdexcept>
#include <iostream>

#include "Application/GameApp.h"
#include "Diagnostics/Logger.h"

int main()
{
    // Enable memory leak checking at program exit
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    // Starts the log, and ends it when main returns: the closing banner, a final flush
    // and the file closed properly. Declared before the game so it is destroyed AFTER it,
    // and the engine's own shutdown messages still reach the file. Without it the logger
    // starts itself on the first message and is never shut down.
    Crucible::Debug::LogSession logSession{};

    GameApp game;

    if (!game.Init())
    {
        CRU_ERROR("Application initialisation failed");
        return 1;
    }

    try
    {
        game.Run();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

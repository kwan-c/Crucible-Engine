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

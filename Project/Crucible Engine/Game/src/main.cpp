#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#include <iostream>

#include "Application/GameApp.h"

int main()
{

    // Enable memory leak checking at program exit
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    GameApp game;
    
    if (!game.Init()) 
    {
        std::cout << "Application initialization failed...\n";
        return 1;
    }
    int ret = game.Run();
    
    return ret;
}
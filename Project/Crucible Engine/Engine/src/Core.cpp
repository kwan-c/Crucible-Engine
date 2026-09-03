#include "Core.h"
#include "WindowTemp.h"

#include <iostream>

namespace Crucible
{
    int EngineApp::run()
    {
        std::cout << "The Hearth smoulders..." << std::endl;
        return CreateWindow();
    }
}
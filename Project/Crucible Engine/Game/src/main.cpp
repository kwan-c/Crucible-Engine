#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#include <iostream>

#include "Core.h"

int main() {
    
    // Enable memory leak checking at program exit
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    Crucible::EngineApp engine;
    int ret = engine.run();
    
    return ret;
}
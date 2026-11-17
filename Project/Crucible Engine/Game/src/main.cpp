#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#include <exception>

#include "Application/GameApp.h"
#include "Diagnostics/Logger.h"
#include "Diagnostics/FatalError.h"
#include "AssetPipe/AssetPaths.h"

// Last, so its macros (ERROR, LoadImage, ...) cannot clash with engine names. Debug only:
// it is just for writing the leak report to a file.
#ifdef _DEBUG
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

int main()
{
    // Enable memory leak checking at program exit
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#ifdef _DEBUG
    // The leak report is written at the very end of the process, after the console and the
    // log are gone, so normally it is only in Visual Studio's Output window. Also write it to
    // MemoryLeaks.txt beside the .exe (empty file = no leaks). The handle is left open on
    // purpose: the report comes after main returns, and the OS closes it.
    {
        HANDLE leakFile = CreateFileW(L"MemoryLeaks.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (leakFile != INVALID_HANDLE_VALUE)
        {
            _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
            _CrtSetReportFile(_CRT_WARN, leakFile);
        }
    }
    // To stop in the debugger on a leaked block's allocation, put its {number} here:
    // _CrtSetBreakAlloc(12345);
#endif

    // Starts the log, and ends it when main returns: the closing banner, a final flush
    // and the file closed properly. First, and outside the try below, so it is open for
    // the asset check's messages and destroyed AFTER the game - the engine's own shutdown
    // messages still reach the file. Without it the logger starts itself on the first
    // message and is never shut down.
    Crucible::Debug::LogSession logSession{};

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

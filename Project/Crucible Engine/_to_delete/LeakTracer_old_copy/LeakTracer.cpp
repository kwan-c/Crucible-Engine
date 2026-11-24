#include "LeakTracer.h"

#ifdef _DEBUG

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    constexpr int kMaxTargets = 512;
    constexpr int kMaxFrames  = 40;

    struct Target
    {
        long   request      = 0;
        size_t expectedSize = 0;   // from the report
        size_t size         = 0;   // seen this run
        bool   hit          = false;
        USHORT frames       = 0;
        void*  stack[kMaxFrames]{};
    };

    // Fixed storage: the hook runs inside the CRT heap and must not allocate.
    Target          g_targets[kMaxTargets];
    int             g_count    = 0;
    _CRT_ALLOC_HOOK g_previous = nullptr;
    bool            g_installed = false;

    int __cdecl AllocHook(int allocType, void* userData, size_t size, int blockType,
                          long request, const unsigned char* file, int line)
    {
        if (allocType == _HOOK_ALLOC)
        {
            for (int i = 0; i < g_count; ++i)
            {
                Target& t = g_targets[i];
                if (t.request == request && !t.hit)
                {
                    t.hit    = true;
                    t.size   = size;
                    t.frames = CaptureStackBackTrace(1, kMaxFrames, t.stack, nullptr);
                    break;
                }
            }
        }
        return g_previous ? g_previous(allocType, userData, size, blockType, request, file, line) : TRUE;
    }

    void ReadPreviousReport()
    {
        FILE* in = nullptr;
        if (fopen_s(&in, "MemoryLeaks.txt", "r") != 0 || !in) { return; }
        char line[512];
        while (g_count < kMaxTargets && fgets(line, sizeof(line), in))
        {
            long request = 0;
            unsigned long long address = 0;
            size_t size = 0;
            // {138650} normal block at 0x00000245A34030B0, 56 bytes long.
            if (sscanf_s(line, "{%ld} normal block at 0x%llx, %zu bytes", &request, &address, &size) == 3)
            {
                g_targets[g_count].request      = request;
                g_targets[g_count].expectedSize = size;
                ++g_count;
            }
        }
        fclose(in);
    }

    void WriteStacks()
    {
        FILE* out = nullptr;
        if (fopen_s(&out, "LeakStacks.txt", "w") != 0 || !out) { return; }

        if (g_count == 0)
        {
            fprintf(out, "No leaks in the previous MemoryLeaks.txt - nothing to trace.\n");
            fclose(out);
            return;
        }

        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        const bool symbols = SymInitialize(process, nullptr, TRUE) == TRUE;

        alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
        int traced = 0;
        for (int i = 0; i < g_count; ++i)
        {
            const Target& t = g_targets[i];
            if (!t.hit)
            {
                fprintf(out, "{%ld} %zu bytes: not allocated this run (allocation numbers shifted)\n\n", t.request, t.expectedSize);
                continue;
            }
            ++traced;
            fprintf(out, "{%ld} %zu bytes%s\n", t.request, t.size,
                    t.size == t.expectedSize ? "" : "   <-- SIZE DIFFERS FROM REPORT: numbers shifted, this is another allocation");
            for (USHORT f = 0; f < t.frames; ++f)
            {
                const DWORD64 address = reinterpret_cast<DWORD64>(t.stack[f]);
                const char* name = "?";
                if (symbols)
                {
                    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
                    std::memset(symbolBuffer, 0, sizeof(symbolBuffer));
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol->MaxNameLen   = 511;
                    DWORD64 displacement = 0;
                    if (SymFromAddr(process, address, &displacement, symbol)) { name = symbol->Name; }
                }
                IMAGEHLP_LINE64 lineInfo{};
                lineInfo.SizeOfStruct = sizeof(lineInfo);
                DWORD lineDisplacement = 0;
                if (symbols && SymGetLineFromAddr64(process, address, &lineDisplacement, &lineInfo))
                    fprintf(out, "    %s  (%s:%lu)\n", name, lineInfo.FileName, lineInfo.LineNumber);
                else
                    fprintf(out, "    %s  [0x%llx]\n", name, static_cast<unsigned long long>(address));
            }
            fprintf(out, "\n");
        }
        fprintf(out, "%d of %d leaked allocations traced.\n", traced, g_count);
        if (symbols) { SymCleanup(process); }
        fclose(out);
    }
}

namespace LeakTracer
{
    Session::Session()
    {
        ReadPreviousReport();
        if (g_count > 0)
        {
            g_previous  = _CrtSetAllocHook(AllocHook);
            g_installed = true;
        }
    }

    Session::~Session()
    {
        if (g_installed) { _CrtSetAllocHook(g_previous); g_installed = false; }
        WriteStacks();
    }
}

#else   // Release: nothing.

namespace LeakTracer
{
    Session::Session()  {}
    Session::~Session() {}
}

#endif

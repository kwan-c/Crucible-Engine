#include "LogSink.h"

#include <iostream>
#include <string>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

namespace Crucible::Debug
{
    // ---------------------------------------------------------------- Console

    void ConsoleSink::Write(const std::string_view& line)
    {
        // '\n' rather than std::endl: endl flushes on every single call,
        // which is slow. The logger flushes explicitly when it matters.
        std::cout << line << '\n';
    }

    void ConsoleSink::Flush()
    {
        std::cout.flush();
    }

    // ------------------------------------------------------- VS Output window

    void DebugOutputSink::Write(const std::string_view& line)
    {
#ifdef _WIN32
        // OutputDebugStringA needs a null-terminated string; a string_view
        // is not guaranteed to be one, so we make a copy.
        std::string text{ line };
        text += '\n';
        OutputDebugStringA(text.c_str());
#else
        (void)line;
#endif
    }

    // ------------------------------------------------------------------- File

    FileSink::FileSink(const std::filesystem::path& path)
        : m_file(path, std::ios::out | std::ios::trunc)
    {
    }

    void FileSink::Write(const std::string_view& line)
    {
        if (m_file.is_open())
        {
            m_file << line << '\n';
        }
    }

    void FileSink::Flush()
    {
        if (m_file.is_open())
        {
            m_file.flush();
        }
    }

    bool FileSink::IsOpen() const
    {
        return m_file.is_open();
    }
}

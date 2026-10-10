/*************************************************************************
* \file:         LogSink.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          12 September 2026
* \version       1.0
* \brief
*                Where log lines go: the ISink interface, plus the
*                console, Visual Studio Output window and log file sinks.
*
* \details
*                The logger keeps a list of sinks and hands every line to
*                each one, so a new destination (an editor console, a
*                profiler) is one new class here and no change to
*                Logger.cpp.
*
*                Engine-internal and not exported from the DLL, which
*                keeps std::ofstream off the DLL boundary.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

// A "sink" is simply a place log text comes out. The logger holds a list of
// them and does not care what any of them are - so adding a new destination
// later (Tracy's timeline, an editor console panel) means writing one small
// class here, and never touching Logger.cpp.
//
// These types deliberately are NOT exported from the DLL: they live inside
// the engine, which keeps std::ofstream and friends off the DLL boundary.

namespace Crucible::Debug
{
    class ISink
    {
    public:
        // Virtual, or deleting through an ISink* skips the derived cleanup.
        virtual ~ISink() = default;

        virtual void Write(const std::string_view& line) = 0;
        virtual void Flush() {}     // most sinks have nothing to do here
    };

    // stdout. Note: invisible in a Release build, because the project links
    // Release with /SUBSYSTEM:WINDOWS and there is no console window.
    class ConsoleSink final : public ISink
    {
    public:
        void Write(const std::string_view& line) override;
        void Flush() override;
    };

    // Visual Studio's Output window. Works in every configuration, including
    // Release, which is why it earns its place alongside the console.
    class DebugOutputSink final : public ISink
    {
    public:
        void Write(const std::string_view& line) override;
    };

    // A plain text file, opened once at startup and truncated.
    class FileSink final : public ISink
    {
    public:
        explicit FileSink(const std::filesystem::path& path);

        void Write(const std::string_view& line) override;
        void Flush() override;

        bool IsOpen() const;

    private:
        std::ofstream m_file;
    };
}

#pragma once
/*************************************************************************
* \file   LeakTracer.h
* \brief  Debug builds only: finds where leaked memory was allocated.
*
*         The CRT leak report (MemoryLeaks.txt, see main.cpp) only gives each
*         leaked block's allocation number, e.g. {138650}. On startup this reads
*         the numbers from the PREVIOUS run's MemoryLeaks.txt and records the call
*         stack when an allocation with one of those numbers happens again. When
*         the session object is destroyed (end of main) it writes the stacks, with
*         function names and source lines, to LeakStacks.txt beside the .exe.
*
*         Allocation numbers only repeat if the run does the same things in the
*         same order (same scene, quit the same way). Each stack is checked
*         against the block size in the report and marked if it does not match.
*         Release builds: does nothing.
**************************************************************************/
namespace LeakTracer
{
    class Session
    {
    public:
        Session();    // reads MemoryLeaks.txt (call before it is overwritten) and installs the hook
        ~Session();   // removes the hook and writes LeakStacks.txt
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
    };
}

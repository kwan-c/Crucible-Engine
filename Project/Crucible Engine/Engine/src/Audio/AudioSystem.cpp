#include "AudioSystem.h"

// Windows - only for CoInitializeEx / CoUninitialize. The two defines keep
// windows.h from dragging in half the OS and from defining min and max as
// macros, which would break std::min and std::clamp in any file after it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

// FMOD - fmod_errors.h defines FMOD_ErrorString inside the header itself, so
// it belongs in exactly one .cpp: this one, next to Check, its only user.
#include <fmod_errors.h>

// Crucible engine
#include "Audio/AudioInternal.h"
#include "Diagnostics/Logger.h"

namespace {

    // How many voices FMOD keeps track of at once - Unity's default "Max
    // Virtual Voices". Past this, starting a sound steals the quietest one.
    // Only the loudest few dozen are actually mixed; the rest are counted but
    // silent, which is why this can be generous.
    constexpr int kMaxVoices = 512;

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    Audio::Audio() : m_impl{ new Impl() } {}

    Audio::~Audio() {
        Shutdown();
        delete m_impl;
    }

    bool Audio::IsInitialised() const { return m_impl->m_initialised; }

    /*!
        \brief
        The one gate every FMOD result goes through.
        \param result
        What the FMOD call returned.
        \param what
        Which call it was, for the log.
        \return
        True on FMOD_OK. False otherwise, with a log line unless the error only
        means a voice has already ended.
    */
    bool Audio::Impl::Check(FMOD_RESULT result, const char* what) {

        if (result == FMOD_OK) return true;

        // A finished voice's handle answers INVALID_HANDLE, and one FMOD took
        // for a newer sound answers CHANNEL_STOLEN. Both are everyday events,
        // not bugs, and logging them would bury the log in one gunfight.
        if (result == FMOD_ERR_INVALID_HANDLE || result == FMOD_ERR_CHANNEL_STOLEN) return false;

        CRU_ERROR("[FMOD] {} failed: {}", what, FMOD_ErrorString(result));
        return false;

    }   // Audio::Impl::Check()

    /*!
        \brief
        Starts FMOD and gives every bus its ChannelGroup.
        \return
        False, with the reason in the log, if FMOD will not start. The game
        then runs silent: every other function is a no-op until Init succeeds.
    */
    bool Audio::Init() {

        Impl& impl = *m_impl;
        if (impl.m_initialised) return true;    // a second Init is a no-op, not a second FMOD

        // COM ------------------------------------------------------------------- //
        // FMOD's Windows output (WASAPI) is built on COM, which must be started
        // on this thread before the first FMOD call. S_FALSE means someone
        // already started it the same way - still ours to balance. A different
        // mode (RPC_E_CHANGED_MODE) is not ours to undo, and FMOD copes with it.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        impl.m_comInitialised = SUCCEEDED(com);
        if (FAILED(com) && com != RPC_E_CHANGED_MODE)
            CRU_WARN("Audio::Init: CoInitializeEx failed (0x{:08X}) - FMOD may not find an output device", static_cast<unsigned long>(com));

        // FMOD ------------------------------------------------------------------ //
        // FMOD_VERSION is the header we compiled against. System_Create
        // compares it with fmod.dll and fails if they differ, which is how a
        // stale DLL next to the exe shows up as a log line instead of a crash.
        if (!Impl::Check(FMOD::System_Create(&impl.m_system, FMOD_VERSION), "Audio::Init: System_Create")) {
            impl.ReleaseAll();
            return false;
        }

        // Right-handed because the engine is. Left-handed, which is FMOD's
        // default, would swap every sound's left and right.
        if (!Impl::Check(impl.m_system->init(kMaxVoices, FMOD_INIT_NORMAL | FMOD_INIT_3D_RIGHTHANDED, nullptr), "Audio::Init: System::init")) {
            impl.ReleaseAll();
            return false;
        }

        // THE BUSES ------------------------------------------------------------- //
        // Master is FMOD's own group, which always exists. Every group we
        // create is plugged into it automatically, which is what gives the
        // diagram in AudioBuses.h: turn Master down and everything follows.
        if (!Impl::Check(impl.m_system->getMasterChannelGroup(&impl.m_buses[static_cast<std::size_t>(AudioBus::Master)]), "Audio::Init: getMasterChannelGroup")) {
            impl.ReleaseAll();
            return false;
        }

        for (std::size_t i = 0; i < kAudioBusCount; ++i) {

            const auto bus = static_cast<AudioBus>(i);
            if (bus == AudioBus::Master) continue;

            // Named after the bus, so FMOD's profiler shows "Music", not a blank.
            if (!Impl::Check(impl.m_system->createChannelGroup(ToString(bus), &impl.m_buses[i]), "Audio::Init: createChannelGroup")) {
                impl.ReleaseAll();
                return false;
            }
        }

        // Slot 0 stays empty so that index 0 can mean "no clip".
        impl.m_clips.clear();
        impl.m_clips.emplace_back();
        impl.m_freeClipSlots.clear();

        impl.m_initialised = true;
        CRU_INFO("Audio started: FMOD {:x}.{:02x}.{:02x}, room for {} voices",
                 FMOD_VERSION >> 16, (FMOD_VERSION >> 8) & 0xFF, FMOD_VERSION & 0xFF, kMaxVoices);
        return true;

    }   // Audio::Init()

    /*!
        \brief
        Takes everything down in the reverse order Init built it. Safe to call
        twice, and the destructor calls it for you.
    */
    void Audio::Shutdown() {

        Impl& impl = *m_impl;
        if (!impl.m_initialised) return;

        impl.ReleaseAll();
        CRU_INFO("Audio stopped");

    }   // Audio::Shutdown()

    /*!
        \brief
        Frees whatever exists, in the one order FMOD accepts. Each step checks
        for null, so a half-finished Init can call this too.
    */
    void Audio::Impl::ReleaseAll() {

        // Sounds first. Releasing one also stops every voice still playing it,
        // and that has to happen while FMOD is still running. Nothing warns
        // about a missed one: the CRT leak check cannot see FMOD's memory.
        for (Clip& slot : m_clips)
            if (slot.m_sound != nullptr) Check(slot.m_sound->release(), "Audio::Shutdown: Sound::release");
        m_clips.clear();
        m_freeClipSlots.clear();

        // Then the groups we created. [Master] is FMOD's and goes with FMOD -
        // releasing it here would free it twice.
        for (std::size_t i = 0; i < kAudioBusCount; ++i) {
            if (static_cast<AudioBus>(i) != AudioBus::Master && m_buses[i] != nullptr)
                Check(m_buses[i]->release(), "Audio::Shutdown: ChannelGroup::release");
            m_buses[i] = nullptr;
        }

        // Then FMOD itself. release() also closes the output device.
        if (m_system != nullptr) {
            Check(m_system->release(), "Audio::Shutdown: System::release");
            m_system = nullptr;
        }

        // Last, and only if Init's CoInitializeEx was ours to balance.
        if (m_comInitialised) {
            CoUninitialize();
            m_comInitialised = false;
        }

        m_initialised = false;

    }   // Audio::Impl::ReleaseAll()

    /*!
        \brief
        FMOD's once-a-frame tick: applies this frame's changes, recomputes 3D
        panning, and hands finished voices back. Skip it and moving 3D sounds
        stay where they started, and finished voices are never reclaimed.
    */
    void Audio::Update() {

        Impl& impl = *m_impl;
        if (!impl.m_initialised) return;

        Impl::Check(impl.m_system->update(), "Audio::Update: System::update");

    }   // Audio::Update()

}   // NAMESPACE CRUCIBLE

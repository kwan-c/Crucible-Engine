#pragma once

// STL
#include <string>

// Crucible engine
#include "AssetPipe/Assetmanager.h"
#include "Audio/AudioSystem.h"
#include "Diagnostics/Logger.h"
#include "Systems/Layer.h"

// ---------------------------------------------------------------------------
// AudioTestLayer - TEMPORARY. Delete this file, and the PushLayer line in
// GameApp::Init, before pushing.
//
// Drives Crucible::Audio directly, with no ECS, so an FMOD, DLL or path
// problem shows up on its own. It owns its own Audio and its own
// AssetManager because nothing else owns them yet - ECSAudioSystem will own
// the Audio, and Core will one day own the one AssetManager.
//
// What you should hear, and what the log should say, is written into the
// log first, so a run can be checked against it:
//
//   1. the cannon once, on SFX (DecompressOnLoad)
//   2. when that ends: the 15 s footsteps, streamed and looping, on Music.
//      They should start over by themselves at about 15 s
//   3. 18 s later: Music bus at 0.25, so they carry on quieter
//   4. 4 s later: stopped and unloaded, then two PASS lines
// ---------------------------------------------------------------------------

class AudioTestLayer final : public Crucible::Layer {

public:

    void OnAttach() override {

        using namespace Crucible;

        CRU_INFO("[AudioTest] expect: 'Audio started', the cannon once, then footsteps that restart at ~15 s, "
                 "quieter at 18 s, then silence and two PASS lines");

        if (!m_audio.Init()) {
            CRU_ERROR("[AudioTest] FAIL: Audio::Init - see the [FMOD] line above");
            return;
        }

        // The asset browser does the same: its own AssetManager, Init to find
        // the Assets folder, Resolve to turn a virtual path into a real one.
        if (!m_assets.Init()) {
            CRU_ERROR("[AudioTest] FAIL: no Assets folder found - run from the project folder or set CRUCIBLE_ASSET_ROOT");
            return;
        }

        // Both load types: the short effect decoded into memory, and the long
        // one streamed from disk, standing in for music.
        m_shot  = m_audio.LoadClip(m_assets.Resolve("Audio/184651__isaac200000__cannon5.wav").string());
        m_music = m_audio.LoadClip(m_assets.Resolve("Audio/837389__artem_zaitsev__heavy-robot-footsteps.wav").string(),
                                   AudioClipLoadType::Streaming);

        if (!m_shot.IsValid() || !m_music.IsValid()) {
            CRU_ERROR("[AudioTest] FAIL: LoadClip - see the [FMOD] line above");
            return;
        }

        m_voice = m_audio.Play(m_shot);     // the default desc: 2D, SFX, volume 1, no loop
        m_step  = Step::OneShot;
        CRU_INFO("[AudioTest] step 1: one shot on SFX");

    }   // AudioTestLayer::OnAttach()

    void OnUpdate(float deltaTime) override {

        using namespace Crucible;

        m_audio.Update();                   // ECSAudioSystem's job later; nobody else calls it yet
        m_timer += deltaTime;

        switch (m_step) {

            case Step::OneShot:
                if (m_audio.IsPlaying(m_voice)) break;
                {
                    AudioPlayDesc desc;
                    desc.m_bus  = AudioBus::Music;
                    desc.m_loop = true;
                    m_voice = m_audio.Play(m_music, desc);
                }
                Next(Step::Looping, "step 2: footsteps streamed, looping, on Music - they should restart at ~15 s");
                break;

            case Step::Looping:
                if (m_timer < 18.0f) break;
                m_audio.SetBusVolume(AudioBus::Music, 0.25f);
                Next(Step::Quiet, "step 3: Music bus at 0.25 - they should be quieter");
                break;

            case Step::Quiet:
                if (m_timer < 4.0f) break;
                m_audio.Stop(m_voice);
                m_audio.UnloadClip(m_music);
                {
                    // The two things a handle promises: a stale clip plays nothing,
                    // and a stopped voice says so. Update first, so FMOD has
                    // actually processed the stop.
                    m_audio.Update();
                    const bool staleRefused = !m_audio.Play(m_music).IsValid();
                    const bool voiceStopped = !m_audio.IsPlaying(m_voice);
                    CRU_INFO("[AudioTest] {}: Play with an unloaded clip's handle did nothing", staleRefused ? "PASS" : "FAIL");
                    CRU_INFO("[AudioTest] {}: IsPlaying is false after Stop",                 voiceStopped ? "PASS" : "FAIL");
                }
                Next(Step::Done, "done - silence from here on");
                break;

            case Step::NotStarted:
            case Step::Done:
                break;
        }

    }   // AudioTestLayer::OnUpdate()

private:

    enum class Step { NotStarted, OneShot, Looping, Quiet, Done };

    void Next(Step step, const char* message) {
        m_step  = step;
        m_timer = 0.0f;
        CRU_INFO("[AudioTest] {}", message);
    }

    Crucible::Audio        m_audio;
    Crucible::AssetManager m_assets;
    Crucible::AudioClip    m_shot;
    Crucible::AudioClip    m_music;
    Crucible::AudioVoice   m_voice;
    Step                   m_step  = Step::NotStarted;
    float                  m_timer = 0.0f;

};  // AUDIOTESTLAYER CLASS

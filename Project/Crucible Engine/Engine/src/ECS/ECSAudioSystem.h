/*************************************************************************
* \file:         ECSAudioSystem.h
* \author:       Hugo
* \date          1 October 2026
* \version       1.0
* \brief
*                The ECS side of audio: owns the engine's one
*                Crucible::Audio and ticks it once per frame.
*
* \details
*                Same split as physics. Crucible::Audio wraps FMOD and
*                knows nothing about entities; this class is where the
*                ECS meets it. Each Update():
*                  1. puts the ears on the AudioListener entity - or, with
*                     none, on the Renderer's camera
*                  2. for each AudioSource: plays it on awake, carries
*                     out Play/Stop/PlayOneShot, passes on volume, pitch
*                     and position changes, and writes back isPlaying
*                  3. silences sources whose entity is gone
*                  4. ticks FMOD, once
*
*                Gameplay edits components. Code that wants the audio
*                engine itself reaches it through
*                Core::Systems().FindSystem<ECSAudioSystem>()->GetAudio(),
*                the way physics is reached through GetPhysics().
*
* \history
*    -v1.0       owns Audio, ticks it, editor preview
*    -v1.1       AudioSource and AudioListener components; edit mode
*    -v1.2       with no AudioListener entity, the ears follow the camera
*
**************************************************************************/
#pragma once

// STL
#include <memory>
#include <string>

// Crucible engine
#include "Audio/AudioSystem.h"
#include "EngineExport.h"
#include "Systems/IECSSystems.h"

namespace Crucible {

    class AssetManager;
    class Renderer;

    /*
        Only the functions the Editor and Game call are exported (ENGINE_API on
        each one), not the whole class. Exporting the class would drag its
        unique_ptr member and the header-only IECSSystem base across the DLL
        boundary, which /W4 reports as C4251 and C4275. Everything else is
        reached through the IECSSystem vtable, which lives in Engine.dll anyway.
    */
    class ECSAudioSystem final : public IECSSystem {

    public:

        // Core's AssetManager, so clips can be named by their virtual path
        // ("Audio/cannon5.wav") and resolved here. It must outlive this system;
        // Core owns both and destroys systems first.
        //
        // The Renderer is optional and only lends its camera: the camera is not
        // an entity yet, so it cannot carry an AudioListener. With no listener
        // entity in the scene, the ears follow it - where Unity puts the
        // AudioListener by default. An AudioListener entity still wins. Once
        // the camera is an entity, give it the component and drop this.
        explicit ECSAudioSystem(AssetManager& assets, const Renderer* renderer = nullptr);
        ~ECSAudioSystem() override;

        ECSAudioSystem(const ECSAudioSystem&)            = delete;
        ECSAudioSystem& operator=(const ECSAudioSystem&) = delete;

        // --- IECSSystem ---------------------------------------------------- //

        void Init() override;
        void Update(World& world, CommandBuffer& commands) override;
        void Shutdown() noexcept override;
        void Describe(SystemDesc& desc) const override;

        // Last, after gameplay and physics have moved things and the hierarchy
        // has worked out world positions - which is what 3D sound reads.
        SystemStage Stage() const override { return SystemStage::PreRender; }

        // --- for gameplay, the editor and tests ------------------------------ //

        ENGINE_API Audio& GetAudio();

        // The editor has no Play button yet, and systems run while you edit,
        // so AudioSources would start sounding as the scene is built. In edit
        // mode they are still tracked but never started: playOnAwake and
        // Play/PlayOneShot requests are dropped, and anything sounding stops.
        // Previews are not affected. The game never calls this; the editor
        // turns it on at startup (EditorManager::Init). A future Play button
        // must turn it off on Play - which lets every playOnAwake fire, as at
        // a scene start - and back on on Stop.
        ENGINE_API void SetEditMode(bool editing);
        ENGINE_API bool IsEditMode() const;

        // Unity's clip preview, the one at the bottom of the Inspector when a
        // clip is selected. One clip at a time: loading another unloads the
        // last, so clicking through fifty clips never holds more than one in
        // memory. It plays 2D, straight into Master, so the Music and SFX
        // sliders do not change what you hear.
        //
        // Load is "the clip was selected"; Play and Stop are the button.
        // Loading the clip that is already loaded changes nothing, so a
        // playing preview carries on when its tile is clicked again.
        ENGINE_API bool  LoadPreview(const std::string& virtualPath);   // false, with a log line, if it will not load
        ENGINE_API void  PlayPreview();                                  // from the start, like Unity's play button
        ENGINE_API void  StopPreview();
        ENGINE_API void  UnloadPreview();                                // selection moved on
        ENGINE_API bool  IsPreviewPlaying() const;
        ENGINE_API float GetPreviewLength() const;                       // seconds, 0 if nothing is loaded

    private:

        struct State;                               // everything else, in the .cpp
        std::unique_ptr<State> m_state;

    };  // ECSAUDIOSYSTEM CLASS

}   // NAMESPACE CRUCIBLE

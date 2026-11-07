/*************************************************************************
* \file:         ECSAudioSystem.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          1 October 2026
* \version       1.4
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
*    -v1.3       SetPaused for play-mode Pause; Core drives edit mode
*    -v1.4       takes the player's bus volumes (UserSettings)
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

    class ResourceManager;
    class Renderer;
    struct AudioSettings;

    /*
        Only the functions the Editor and Game call are exported (ENGINE_API on
        each one), not the whole class. Exporting the class would drag its
        unique_ptr member and the header-only IECSSystem base across the DLL
        boundary, which /W4 reports as C4251 and C4275. Everything else is
        reached through the IECSSystem vtable, which lives in Engine.dll anyway.
    */
    class ECSAudioSystem final : public IECSSystem {

    public:

        // Core's ResourceManager: clips are named by their asset GUID (or, for
        // the editor preview, their virtual path "Audio/cannon5.wav") and found
        // through it - the audio system never asks the AssetManager itself.
        // FMOD still opens the file (music streams from disk while it plays)
        // and owns the sound memory; this system shares one clip per file. It
        // must outlive this system; Core owns both and destroys systems first.
        //
        // The Renderer is optional and only lends its camera: the camera is not
        // an entity yet, so it cannot carry an AudioListener. With no listener
        // entity in the scene, the ears follow it - where Unity puts the
        // AudioListener by default. An AudioListener entity still wins. Once
        // the camera is an entity, give it the component and drop this.
        //
        // `volumes` is the player's Master / Music / SFX (Core's UserSettings),
        // read every frame so an options-menu slider is heard at once. Like
        // `resources`, it must outlive this system; Core owns it.
        ECSAudioSystem(ResourceManager& resources, const AudioSettings& volumes, const Renderer* renderer = nullptr);
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

        // Audio keeps running while you edit (previews need it), so
        // AudioSources would start sounding as the scene is built. In edit
        // mode they are still tracked but never started: playOnAwake and
        // Play/PlayOneShot requests are dropped, and anything sounding stops.
        // Previews are not affected. Core owns this: on in Edit, off on Play -
        // which lets every playOnAwake fire, as at a scene start - and back
        // on at Stop (see Core::RequestPlay / RequestStop).
        ENGINE_API void SetEditMode(bool editing);
        ENGINE_API bool IsEditMode() const;

        // Play-mode Pause. Every voice a source started freezes where it is,
        // and requests (Play, Stop, PlayOneShot, playOnAwake) wait in the
        // component until Resume instead of being dropped. Previews are not
        // affected. Core turns it on at Pause and off at Resume and Stop.
        ENGINE_API void SetPaused(bool paused);
        ENGINE_API bool IsPaused() const;

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

        // Closes every audio file the engine has open at virtualPath (a file, or
        // every file under a folder): the preview and any cached clips. Windows
        // will not rename or delete an open file, and a streamed clip - the
        // preview always is one - keeps its file open while loaded. The editor
        // calls this before renaming, moving or deleting. Returns true if the
        // preview was one of them (so the caller can load it again).
        ENGINE_API bool  ReleaseFiles(const std::string& virtualPath);

    private:

        struct State;                               // everything else, in the .cpp
        std::unique_ptr<State> m_state;

    };  // ECSAUDIOSYSTEM CLASS

}   // NAMESPACE CRUCIBLE

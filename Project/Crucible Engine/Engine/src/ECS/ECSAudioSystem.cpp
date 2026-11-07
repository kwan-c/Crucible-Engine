/*************************************************************************
* \file:         ECSAudioSystem.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          1 October 2026
* \version       1.6
* \brief
*                Implementation of ECSAudioSystem. See the header for
*                what happens each frame.
*
* \details
*                The system keeps one Record per entity with an
*                AudioSource: the voices it started and the values it
*                last sent them. Each frame the component is compared
*                against that record, and only differences reach FMOD.
*                An entity the query no longer finds - destroyed, or its
*                AudioSource removed - has its voices stopped and its
*                record dropped, the same "absence is the signal" rule
*                ECSPhysicsSystem uses.
*
*                Clips come from the ResourceManager (GetAudioClip): loaded
*                the first time a source needs one, shared by every source
*                that names the same file, and freed when a scene change
*                leaves them unused. The editor preview keeps its own
*                private clip (see LoadPreview).
*
* \history
*    -v1.0       owns Audio, ticks it, editor preview
*    -v1.1       AudioSource and AudioListener components; edit mode
*    -v1.2       with no AudioListener entity, the ears follow the camera
*    -v1.3       SetPaused: voices freeze, requests wait for Resume
*    -v1.4       fix: file paths reach FMOD as UTF-8, so an accented
*                folder name no longer hides every sound under it
*    -v1.5       the player's bus volumes (UserSettings) reach the buses
*    -v1.6       PlayOneShot(clip, volumeScale): AudioSource's queued per-shot clips
*
**************************************************************************/
#include "ECSAudioSystem.h"

// STL
#include <filesystem>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

// Crucible engine
#include "Resources/ResourceManager.h"
#include "Components/Components.h"
#include "Configuration/UserSettings.h"
#include "Diagnostics/Logger.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/RenderSystem.h"
#include "Systems/Query.h"
#include "World.h"

namespace {

    using namespace Crucible;

    // Read-only on purpose: a mutable query stamps every AudioSource as
    // changed every frame. The few writes go through GetComponent instead,
    // and only when a value actually changed.
    using SourceQuery   = Query<const AudioSource, const WorldTransform>;
    using ListenerQuery = Query<const AudioListener, const WorldTransform>;

    // FMOD reads file names as UTF-8. path::string() hands back the old Windows
    // code page instead, so one accented letter anywhere in the path - a user
    // folder named after its owner, say - reaches FMOD as different bytes and
    // the file "does not exist". u8string() is UTF-8; its char8_t bytes are
    // copied into the plain std::string that Audio::LoadClip takes.
    std::string ToUtf8(const std::filesystem::path& path) {
        const std::u8string utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    // The engine's convention, the same one lights use: position is the
    // world matrix's translation, forward its local -Z axis, up its local +Y.
    Math::vec3 PositionOf(const WorldTransform& world) { return Math::vec3(world.matrix[3]); }
    Math::vec3 ForwardOf(const WorldTransform& world)  { return -Math::vec3(world.matrix[2]); }
    Math::vec3 UpOf(const WorldTransform& world)       { return Math::vec3(world.matrix[1]); }

    bool Same(const Math::vec3& a, const Math::vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

    // An AudioSource's settings in the form Audio::Play takes. `loop` is
    // passed separately because a one-shot never loops, whatever the
    // component says.
    AudioPlayDesc ToPlayDesc(const AudioSource& source, const Math::vec3& position, bool loop) {

        AudioPlayDesc desc;
        desc.m_bus          = source.m_bus;
        desc.m_volume       = source.m_volume;
        desc.m_pitch        = source.m_pitch;
        desc.m_loop         = loop;
        desc.m_spatialBlend = source.m_spatialBlend;
        desc.m_position     = position;
        desc.m_minDistance  = source.m_minDistance;
        desc.m_maxDistance  = source.m_maxDistance;
        desc.m_rolloffMode  = source.m_rolloffMode;
        return desc;

    }   // ToPlayDesc()

    // --- one entity's audio, as the system remembers it --------------------- //

    struct Record {

        Entity                  entity   {};
        AudioVoice              voice    {};        // the Play() voice
        std::vector<AudioVoice> oneShots;           // PlayOneShot voices still sounding

        // What the voices were last told, so only changes are sent.
        float                   volume   = 1.0f;
        float                   pitch    = 1.0f;
        Math::vec3              position = Math::vec3(0.0f, 0.0f, 0.0f);

        bool                    awake     = false;  // playOnAwake has had its one chance
        std::uint64_t           seenFrame = 0;      // the last frame the query found this entity

    };  // RECORD STRUCT

    // An isPlaying value to write back, and the requests it has now dealt
    // with. Collected during the query and written after it, so nothing is
    // written into a component while the query is walking over it.
    struct WriteBack {

        Entity entity    {};
        bool   isPlaying = false;

    };  // WRITEBACK STRUCT

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    // ------------------------------------------------------------------- //
    // STATE
    // ------------------------------------------------------------------- //

    struct ECSAudioSystem::State {

        State(ResourceManager& resourceManager, const AudioSettings& playerVolumes, const Renderer* rendererToFollow)
            : resources(resourceManager), volumes(playerVolumes), renderer(rendererToFollow) {}

        ResourceManager&     resources;         // clips are found by GUID through it
        const AudioSettings& volumes;           // Core's UserSettings: the player's bus sliders
        const Renderer*      renderer = nullptr;     // only for its camera, the listener when no entity has one
        Audio           audio;
        World*        world    = nullptr;
        bool          editMode = false;
        bool          paused   = false;         // play-mode Pause: voices frozen, requests wait

        std::optional<SourceQuery>   sources;
        std::optional<ListenerQuery> listeners;
        std::optional<Query<const AudioSource>> offSources;     // switched-off sources (WhereDisabled)
        std::vector<Entity>                     offWrites;      // reused every frame

        std::unordered_map<std::uint64_t, Record> records;      // keyed by EntityToUserData(entity)
        std::vector<WriteBack>                    writeBacks;   // reused every frame

        // The volumes last sent to the buses. -1 is never a real volume, so the
        // first frame after Init always sends all three.
        AudioSettings sentVolumes{ -1.0f, -1.0f, -1.0f };

        std::uint64_t frame               = 0;
        bool          warnedNoListener    = false;
        bool          warnedManyListeners = false;

        // The editor's one preview. Kept apart from everything components
        // play, so stopping a preview can never stop a game sound.
        AudioClip     previewClip;
        AudioVoice    previewVoice;
        std::string   previewPath;     // which asset previewClip is, so selecting it again keeps it

        void      BindWorld(World& ecs);
        void      SyncBuses();
        void      SyncListener();
        void      SyncSource(Record& record, const AudioSource& source, const WorldTransform& transform);
        AudioClip ClipFor(const AudioSource& source);
        void      Silence(Record& record);
        void      RemoveAll();

    };  // ECSAUDIOSYSTEM::STATE STRUCT

    // ------------------------------------------------------------------- //
    // LIFECYCLE
    // ------------------------------------------------------------------- //

    ECSAudioSystem::ECSAudioSystem(ResourceManager& resources, const AudioSettings& volumes, const Renderer* renderer)
        : m_state{ std::make_unique<State>(resources, volumes, renderer) } {}

    ECSAudioSystem::~ECSAudioSystem() { Shutdown(); }

    // A machine with no sound device still runs the game - silently. Audio's
    // own log line above this one says why.
    void ECSAudioSystem::Init() {
        if (!m_state->audio.Init())
            CRU_ERROR("ECSAudioSystem: audio did not start - the game will run silent");
        // Clips are loaded by the ResourceManager into this engine (silent no-ops if it failed).
        m_state->resources.AttachAudio(&m_state->audio);
    }

    void ECSAudioSystem::Shutdown() noexcept {

        if (!m_state) return;
        State& s = *m_state;

        s.RemoveAll();

        // A Query holds pointers into the World's archetypes and must not
        // outlive it, so a restart builds fresh ones.
        s.sources.reset();
        s.listeners.reset();
        s.offSources.reset();
        s.world = nullptr;

        // The ResourceManager unloads the clips it loaded into this engine;
        // Audio::Shutdown then releases anything left (the preview).
        s.resources.AttachAudio(nullptr);
        s.previewClip  = AudioClip{};
        s.previewVoice = AudioVoice{};
        s.previewPath.clear();
        s.sentVolumes = AudioSettings{ -1.0f, -1.0f, -1.0f };   // a restarted FMOD has fresh buses at 1
        s.audio.Shutdown();

    }   // ECSAudioSystem::Shutdown()

    void ECSAudioSystem::Describe(SystemDesc& desc) const {

        // Main thread, always. Init starts COM on the thread that calls it, and
        // the editor's preview calls come from the ImGui thread, which is the
        // main thread. Keeping every FMOD call on one thread means never having
        // to ask whether a call is safe from another.
        desc.Uses<SourceQuery>()                             // reads AudioSource + WorldTransform
            .Uses<ListenerQuery>()                           // reads AudioListener + WorldTransform
            .Writes<AudioSource>()                           // isPlaying, and spent requests, through GetComponent
            .RunAfterIfPresent<HierarchyTransformSystem>()   // WorldTransform must already be this frame's
            .Affinity(SystemAffinity::MainThread);

    }   // ECSAudioSystem::Describe()

    Audio& ECSAudioSystem::GetAudio()          { return m_state->audio; }
    bool   ECSAudioSystem::IsEditMode() const  { return m_state->editMode; }
    bool   ECSAudioSystem::IsPaused() const    { return m_state->paused; }

    /*!
        \brief
        Freezes (true) or resumes (false) every voice an AudioSource started.
    */
    void ECSAudioSystem::SetPaused(bool paused) {

        State& s = *m_state;
        if (s.paused == paused) return;
        s.paused = paused;

        for (auto& [key, record] : s.records) {
            s.audio.SetPaused(record.voice, paused);
            for (const AudioVoice& shot : record.oneShots) s.audio.SetPaused(shot, paused);
        }

    }   // ECSAudioSystem::SetPaused()

    /*!
        \brief
        Turns edit mode on (silent, the editor) or off (playing, the game).
    */
    void ECSAudioSystem::SetEditMode(bool editing) {

        State& s = *m_state;
        if (s.editMode == editing) return;
        s.editMode = editing;

        for (auto& [key, record] : s.records) {
            if (editing) s.Silence(record);     // Stop: everything goes quiet
            else         record.awake = false;  // Play: every source wakes again, as at a scene start
        }

    }   // ECSAudioSystem::SetEditMode()

    // ------------------------------------------------------------------- //
    // THE FRAME
    // ------------------------------------------------------------------- //

    /*!
        \brief
        One frame of audio: ears, then sources, then FMOD's tick.
    */
    void ECSAudioSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

        State& s = *m_state;
        if (!s.audio.IsInitialised()) return;

        s.BindWorld(world);
        ++s.frame;

        s.SyncBuses();
        s.SyncListener();

        s.writeBacks.clear();
        s.sources->ForEach([&](Entity entity, const AudioSource& source, const WorldTransform& transform) {

            auto [it, inserted] = s.records.try_emplace(EntityToUserData(entity));
            Record& record = it->second;
            if (inserted) record.entity = entity;
            record.seenFrame = s.frame;

            s.SyncSource(record, source, transform);
        });

        // After the walk, not during it: isPlaying, and the requests that are
        // now spent. Only components with something to change are touched.
        for (const WriteBack& write : s.writeBacks) {
            if (AudioSource* source = world.GetComponent<AudioSource>(write.entity)) {
                source->m_isPlaying       = write.isPlaying;
                source->m_request         = AudioRequest::None;
                source->m_pendingOneShots = 0;
                source->m_pendingClipShotCount = 0;
            }
        }

        // Switched off: its voices stop below with its record (the query no longer finds it).
        // Unity ignores Play() on a disabled AudioSource; here a request is only a field, so drop
        // what is asked for while off - otherwise it would fire the moment it is switched on.
        s.offWrites.clear();
        s.offSources->ForEach([&](Entity entity, const AudioSource& source) {
            if (source.m_request != AudioRequest::None || source.m_pendingOneShots != 0 || source.m_pendingClipShotCount != 0 || source.m_isPlaying)
                s.offWrites.push_back(entity);
        });
        for (const Entity entity : s.offWrites) {
            if (AudioSource* source = world.GetComponent<AudioSource>(entity)) {
                source->m_isPlaying       = false;
                source->m_request         = AudioRequest::None;
                source->m_pendingOneShots = 0;
                source->m_pendingClipShotCount = 0;
            }
        }

        // Not seen this frame: the entity was destroyed, or lost its
        // AudioSource. Unity stops the sound with it, and so do we.
        for (auto it = s.records.begin(); it != s.records.end();) {
            if (it->second.seenFrame != s.frame) {
                s.Silence(it->second);
                it = s.records.erase(it);
            }
            else {
                ++it;
            }
        }

        s.audio.Update();       // once, after every change above

    }   // ECSAudioSystem::Update()

    /*!
        \brief
        Sends the player's Master / Music / SFX volumes to the buses, but only
        the ones that changed since last frame.
        \details
        Read every frame rather than pushed by a "volume changed" call, so an
        options menu only has to write Core::GetUserSettings().audio and the
        slider is heard at once - there is no second call to forget.
    */
    void ECSAudioSystem::State::SyncBuses() {

        if (volumes.masterVolume != sentVolumes.masterVolume) audio.SetBusVolume(AudioBus::Master, volumes.masterVolume);
        if (volumes.musicVolume  != sentVolumes.musicVolume)  audio.SetBusVolume(AudioBus::Music,  volumes.musicVolume);
        if (volumes.sfxVolume    != sentVolumes.sfxVolume)    audio.SetBusVolume(AudioBus::SFX,    volumes.sfxVolume);
        sentVolumes = volumes;

    }   // ECSAudioSystem::State::SyncBuses()

    // A different world means a different scene: every voice belongs to
    // entities that are gone, so they all stop.
    void ECSAudioSystem::State::BindWorld(World& ecs) {

        if (world == &ecs && sources.has_value()) return;

        RemoveAll();
        world = &ecs;
        sources.emplace(ecs.CreateQuery<const AudioSource, const WorldTransform>());
        listeners.emplace(ecs.CreateQuery<const AudioListener, const WorldTransform>());
        offSources.emplace(ecs.BuildQuery<const AudioSource>().WhereDisabled().Build());
        warnedNoListener    = false;
        warnedManyListeners = false;

    }   // ECSAudioSystem::State::BindWorld()

    /*!
        \brief
        Puts the ears on the first AudioListener. Unity allows one; with more,
        the first wins and the log says so once. With none, they follow the
        Renderer's camera, if there is one.
    */
    void ECSAudioSystem::State::SyncListener() {

        std::uint32_t count = 0;
        listeners->ForEach([&](Entity, const AudioListener&, const WorldTransform& transform) {
            if (count++ == 0) audio.SetListener(PositionOf(transform), ForwardOf(transform), UpOf(transform));
        });

        // No listener entity: the camera, where Unity puts the AudioListener by
        // default. Every frame, because the camera moves without telling anyone.
        if (count == 0 && renderer != nullptr) {
            const Camera& camera = renderer->GetCamera();
            audio.SetListener(camera.position, camera.Forward(), camera.Up());
            return;
        }

        // Only worth saying when something can actually be heard.
        if (editMode || records.empty()) return;

        if (count == 0 && !warnedNoListener) {
            CRU_WARN("ECSAudioSystem: no entity has an AudioListener - 3D sounds are heard from the world origin");
            warnedNoListener = true;
        }
        if (count > 1 && !warnedManyListeners) {
            CRU_WARN("ECSAudioSystem: {} entities have an AudioListener - only the first is used", count);
            warnedManyListeners = true;
        }

    }   // ECSAudioSystem::State::SyncListener()

    /*!
        \brief
        Makes one entity's voices match its AudioSource: requests first, then
        changes to what is already sounding, then isPlaying.
    */
    void ECSAudioSystem::State::SyncSource(Record& record, const AudioSource& source, const WorldTransform& transform) {

        // Paused: nothing starts, stops or changes, and the component is left
        // alone, so its requests (and playOnAwake) run at Resume. Live changes
        // made meanwhile reach the voices then too: the record still holds
        // what they were last sent.
        if (paused) return;

        const Math::vec3 position = PositionOf(transform);
        AudioRequest     request  = source.m_request;
        std::uint8_t     oneShots = source.m_pendingOneShots;
        std::uint8_t     clipShots = source.m_pendingClipShotCount;

        // AWAKE ------------------------------------------------------------------ //
        // The first frame a source exists is its Awake, as when Unity enables
        // it. playOnAwake is a Play request on that frame and never again -
        // unless a script already asked for something itself.
        if (!record.awake) {
            record.awake = true;
            if (source.m_playOnAwake && request == AudioRequest::None) request = AudioRequest::Play;
        }

        // Editing: requests are dropped, not saved for later, so nothing
        // queued while building the scene fires the moment Play is pressed.
        if (editMode) {
            request  = AudioRequest::None;
            oneShots = 0;
            clipShots = 0;
        }

        // REQUESTS --------------------------------------------------------------- //
        if (request == AudioRequest::Stop) {
            Silence(record);
        }
        else if (request == AudioRequest::Play) {
            audio.Stop(record.voice);               // Unity's Play restarts; it does not stack
            const AudioClip clip = ClipFor(source);
            record.voice = clip.IsValid() ? audio.Play(clip, ToPlayDesc(source, position, source.m_loop)) : AudioVoice{};
        }

        if (oneShots > 0) {
            const AudioClip clip = ClipFor(source);
            for (std::uint8_t i = 0; i < oneShots && clip.IsValid(); ++i) {
                const AudioVoice shot = audio.Play(clip, ToPlayDesc(source, position, false));
                if (shot.IsValid()) record.oneShots.push_back(shot);
            }
        }

        // PlayOneShot(clip, volumeScale): another clip, through this source's settings. Always
        // loaded decompressed - a streamed clip plays one voice at a time, wrong for one-shots.
        // Kept with the other one-shots, so Stop and pausing reach them too.
        for (std::uint8_t i = 0; i < clipShots && i < AudioSource::kMaxClipShots; ++i) {
            const AudioClip clip = resources.GetAudioClip(source.m_pendingClipShots[i], AudioClipLoadType::DecompressOnLoad);
            if (!clip.IsValid()) continue;
            AudioPlayDesc desc = ToPlayDesc(source, position, false);
            desc.m_volume *= source.m_pendingClipShotVolumes[i];
            const AudioVoice shot = audio.Play(clip, desc);
            if (shot.IsValid()) record.oneShots.push_back(shot);
        }

        // Anything just started has the component's current values.
        if (request == AudioRequest::Play || oneShots > 0 || clipShots > 0) {
            record.volume   = source.m_volume;
            record.pitch    = source.m_pitch;
            record.position = position;
        }

        // LIVE CHANGES ----------------------------------------------------------- //
        // Volume, pitch and position reach a sound while it plays, as in Unity.
        // Loop, spatialBlend, distances, output and clip take effect at the
        // next Play: Audio has no live setter for them yet.
        std::erase_if(record.oneShots, [&](const AudioVoice& shot) { return !audio.IsPlaying(shot); });

        const bool playing = audio.IsPlaying(record.voice);
        if (playing || !record.oneShots.empty()) {

            const auto eachVoice = [&](const auto& apply) {
                apply(record.voice);
                for (const AudioVoice& shot : record.oneShots) apply(shot);
            };

            if (source.m_volume != record.volume) {
                eachVoice([&](const AudioVoice& voice) { audio.SetVolume(voice, source.m_volume); });
                record.volume = source.m_volume;
            }
            if (source.m_pitch != record.pitch) {
                eachVoice([&](const AudioVoice& voice) { audio.SetPitch(voice, source.m_pitch); });
                record.pitch = source.m_pitch;
            }
            if (source.m_spatialBlend > 0.0f && !Same(position, record.position)) {
                eachVoice([&](const AudioVoice& voice) { audio.SetPosition(voice, position); });
                record.position = position;
            }
        }

        // WRITE BACK -------------------------------------------------------------- //
        // Only when something changed, so a silent source is never stamped as
        // changed. Requests are cleared even in edit mode, so they are spent.
        const bool requested = source.m_request != AudioRequest::None || source.m_pendingOneShots != 0
                             || source.m_pendingClipShotCount != 0;
        if (requested || source.m_isPlaying != playing)
            writeBacks.push_back(WriteBack{ record.entity, playing });

    }   // ECSAudioSystem::State::SyncSource()

    /*!
        \brief
        The clip an AudioSource names, loading it the first time it is needed.
        \return
        An invalid handle for "no clip", a missing file, or one that will not
        load - each reported once.
    */
    AudioClip ECSAudioSystem::State::ClipFor(const AudioSource& source) {

        if (source.m_clip == 0) return AudioClip{};

        // The id is the asset's guid from its .meta file, so a renamed or
        // moved file is still found. The ResourceManager loads, shares and
        // frees clips (and reports a bad one once); asked on every Play, so
        // a clip freed by a scene change is simply loaded again.
        return resources.GetAudioClip(source.m_clip, source.m_loadType);

    }   // ECSAudioSystem::State::ClipFor()

    // Unity's Stop: the Play() voice and every one-shot.
    void ECSAudioSystem::State::Silence(Record& record) {
        audio.Stop(record.voice);
        for (const AudioVoice& shot : record.oneShots) audio.Stop(shot);
        record.voice = AudioVoice{};
        record.oneShots.clear();
    }

    void ECSAudioSystem::State::RemoveAll() {
        for (auto& [key, record] : records) Silence(record);
        records.clear();
    }

    // ------------------------------------------------------------------- //
    // EDITOR PREVIEW
    // ------------------------------------------------------------------- //

    /*!
        \brief
        Makes one audio asset the preview clip, without playing it.
        \param virtualPath
        The asset's path inside Assets, e.g. "Audio/cannon5.wav".
        \return
        True if that clip is now loaded.
    */
    bool ECSAudioSystem::LoadPreview(const std::string& virtualPath) {

        State& s = *m_state;
        if (s.previewClip.IsValid() && s.previewPath == virtualPath) return true;   // already the one: leave it playing

        UnloadPreview();

        // Streamed: opening it only reads the header, so selecting a long
        // track is instant, and a preview is only ever one voice - the one
        // thing a stream cannot do is overlap.
        // Found by GUID through the ResourceManager, but loaded as a private
        // copy rather than GetAudioClip's shared one: a Streaming clip has a
        // single read position, so sharing it with a scene's music would let
        // the preview take over (or stop) the game's voice.
        const AssetId id = s.resources.GetIdForPath(virtualPath);
        if (!s.resources.IsAudio(id)) {
            CRU_WARN("ECSAudioSystem: '{}' is not an audio file in the project (refresh the Asset Browser?)", virtualPath);
            return false;
        }
        s.previewClip = s.audio.LoadClip(ToUtf8(s.resources.GetFilePath(id)), AudioClipLoadType::Streaming);
        if (!s.previewClip.IsValid()) return false;     // LoadClip has already said why

        s.previewPath = virtualPath;
        return true;

    }   // ECSAudioSystem::LoadPreview()

    void ECSAudioSystem::PlayPreview() {

        State& s = *m_state;
        if (!s.previewClip.IsValid()) return;

        // Stop first, so pressing Play mid-clip starts it over rather than
        // asking a stream to play twice.
        s.audio.Stop(s.previewVoice);

        AudioPlayDesc desc;
        desc.m_bus     = AudioBus::Master;          // so the Music and SFX sliders do not change what you hear
        s.previewVoice = s.audio.Play(s.previewClip, desc);

    }   // ECSAudioSystem::PlayPreview()

    void ECSAudioSystem::StopPreview() {
        m_state->audio.Stop(m_state->previewVoice);
        m_state->previewVoice = AudioVoice{};
    }

    // Unloading also stops the voice, so there is no separate Stop here.
    void ECSAudioSystem::UnloadPreview() {
        State& s = *m_state;
        s.audio.UnloadClip(s.previewClip);
        s.previewClip  = AudioClip{};
        s.previewVoice = AudioVoice{};
        s.previewPath.clear();
    }

    bool ECSAudioSystem::ReleaseFiles(const std::string& virtualPath) {

        State& s = *m_state;
        if (virtualPath.empty()) return false;

        const bool previewHit = s.previewClip.IsValid() &&
            (s.previewPath == virtualPath || s.previewPath.rfind(virtualPath + "/", 0) == 0);
        if (previewHit) UnloadPreview();

        s.resources.ReleaseAudioFiles(virtualPath);
        return previewHit;

    }   // ECSAudioSystem::ReleaseFiles()

    bool  ECSAudioSystem::IsPreviewPlaying() const { return m_state->audio.IsPlaying(m_state->previewVoice); }
    float ECSAudioSystem::GetPreviewLength() const { return m_state->audio.GetClipLength(m_state->previewClip); }

}   // NAMESPACE CRUCIBLE

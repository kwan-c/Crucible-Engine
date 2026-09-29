#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/RegisterTypes.h>

// STL
#include <algorithm>    // Init()
#include <cstdarg>      // JoltTrace()
#include <cstdio>       // JoltTrace()
#include <format>       // JoltAssertFailed()
#include <thread>       // Init()

// Crucible engine
#include "Diagnostics/Logger.h"
#include "Physics/JoltConversions.h"
#include "Physics/PhysicsInternal.h"

namespace {

    // Jolt only speaks up when something is off, so this logs as a warning.
    // Jolt's worker threads call it too, which is safe: the logger takes a lock.
    void JoltTrace(const char* fmt, ...) {

        char buffer[1024];

        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        CRU_WARN("[Jolt] {}", buffer);

    }   // JoltTrace()

#ifdef JPH_ENABLE_ASSERTS
    // Logged with Jolt's own file and line, so the log points at the line inside
    // Jolt that objected rather than at this function.
    bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line) {

        Crucible::Debug::LogMessage(Crucible::Debug::Level::Error, file, static_cast<int>(line),
            std::format("[Jolt] assert failed: {} {}", expression, message != nullptr ? message : ""));

        return true;    // true = break into the debugger

    }   // JoltAssertFailed()
#endif

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    using namespace JoltConvert;

    Physics::Physics() : m_impl{ new Impl() } {}

    Physics::~Physics() {
        Shutdown();
        delete m_impl;
    }

    bool Physics::IsInitialised() const { return m_impl->m_initialised; }

    /*!
        \brief
        Builds the Jolt world, in the order Jolt needs it built.
        \param settings
        The limits and gravity, fixed for the lifetime of the world.
        \return
        False, with a line in the log, if the settings make no sense.
    */
    bool Physics::Init(const PhysicsSettings& settings) {

        Impl& impl = *m_impl;
        if (impl.m_initialised) return true;    // a second Init is a no-op, not a second world

        if (settings.m_maxBodies == 0 || settings.m_maxBodyPairs == 0 || settings.m_maxContacts == 0) {
            CRU_ERROR("Physics::Init: max bodies, body pairs and contacts must all be above zero");
            return false;
        }

        // JOLT'S GLOBALS ------------------------------------------------------- //
        // Process-wide, not per-world, and the order matters: the allocator before
        // anything allocates, Trace before RegisterTypes so a mismatched build can
        // explain itself in the log, the factory before the types registering into it.
        JPH::RegisterDefaultAllocator();

        JPH::Trace = JoltTrace;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)

        // This class is the only thing in Crucible that touches Jolt, so it owns
        // the factory outright and Shutdown deletes it. The day something else
        // uses Jolt, these globals need a reference count instead.
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        // THE LAYER TABLES ------------------------------------------------------ //
        const auto layerCount = static_cast<JPH::uint>(kPhysicsLayerCount);

        // Who collides with whom: every pair asked once, from PhysicsLayers.cpp.
        impl.m_layerPairs = std::make_unique<JPH::ObjectLayerPairFilterTable>(layerCount);
        for (JPH::uint a = 0; a < layerCount; ++a)
            for (JPH::uint b = a; b < layerCount; ++b)
                if (LayersCollide(static_cast<PhysicsLayer>(a), static_cast<PhysicsLayer>(b)))
                    impl.m_layerPairs->EnableCollision(static_cast<JPH::ObjectLayer>(a), static_cast<JPH::ObjectLayer>(b));

        // Which tree each layer is filed in: the arena in one, everything else in the other.
        impl.m_layerToTree = std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(layerCount, Impl::kTreeCount);
        for (JPH::uint layer = 0; layer < layerCount; ++layer) {
            const bool isArena = static_cast<PhysicsLayer>(layer) == PhysicsLayer::NonMoving;
            impl.m_layerToTree->MapObjectToBroadPhaseLayer(static_cast<JPH::ObjectLayer>(layer),
                                                           isArena ? Impl::kTreeStatic : Impl::kTreeMoving);
        }

        // Which trees a layer must search. Jolt derives this from the other two.
        impl.m_layerVsTree = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
            *impl.m_layerToTree, Impl::kTreeCount, *impl.m_layerPairs, layerCount);

        // SCRATCH MEMORY AND WORKERS -------------------------------------------- //
        // Jolt calls std::abort() if a step ever needs more scratch than this -
        // not an assert, so it kills Release builds too. 0 means "use the
        // default", matching m_workerThreads, and anything implausibly small is
        // raised rather than trusted: a 4 KB scratch space is a typo.
        constexpr std::size_t kMinTempAllocatorBytes = 1u * 1024 * 1024;
        std::size_t scratch = settings.m_tempAllocatorBytes;
        if (scratch == 0) scratch = PhysicsSettings{}.m_tempAllocatorBytes;
        if (scratch < kMinTempAllocatorBytes) {
            CRU_WARN("Physics::Init: m_tempAllocatorBytes was {} - raising it to {}", scratch, kMinTempAllocatorBytes);
            scratch = kMinTempAllocatorBytes;
        }
        impl.m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(static_cast<JPH::uint>(scratch));

        // 0 means "decide for me". One fewer than the machine has, because the
        // thread calling Step works too - then capped, because at M1's scale the
        // jobs are small and every extra thread is one more to wait for at each
        // barrier. Hyperthreads share execution units, so the top half of that
        // count buys very little on SIMD work like this anyway. The cap is a
        // guess until the week-3 profiler says otherwise; m_workerThreads
        // overrides it whenever you want to measure a different number.
        constexpr int kMaxAutoWorkers = 4;

        int workers = static_cast<int>(settings.m_workerThreads);
        if (workers <= 0) {
            const int hardware = static_cast<int>(std::thread::hardware_concurrency());
            workers = std::clamp(hardware - 1, 1, kMaxAutoWorkers);
        }
        impl.m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);

        // THE WORLD -------------------------------------------------------------- //
        impl.m_world = std::make_unique<JPH::PhysicsSystem>();
        impl.m_world->Init(settings.m_maxBodies, 0, settings.m_maxBodyPairs, settings.m_maxContacts,
                           *impl.m_layerToTree, *impl.m_layerVsTree, *impl.m_layerPairs);
        impl.m_world->SetGravity(ToJolt(settings.m_gravity));

        // Characters live beside the bodies. Slot 0 stays empty so that index 0
        // can mean "no character", the same trick BodyHandle plays with 0xFFFFFFFF.
        impl.m_characterVsCharacter = std::make_unique<JPH::CharacterVsCharacterCollisionSimple>();
        impl.m_characters.clear();
        impl.m_characters.emplace_back();
        impl.m_freeCharacterSlots.clear();

        impl.m_settings    = settings;
        impl.m_initialised = true;

        CRU_INFO("Physics world started: {} worker threads, room for {} bodies", workers, settings.m_maxBodies);
        return true;

    }   // Physics::Init()

    /*!
        \brief
        Takes the world apart in the reverse order Init built it. Safe to call
        twice, and the destructor calls it for you.
    */
    void Physics::Shutdown() {

        Impl& impl = *m_impl;
        if (!impl.m_initialised) return;

        // Characters first: releasing one also removes the hidden body it carries,
        // and that body has to go while the world still exists.
        for (Impl::Character& slot : impl.m_characters) {
            if (!slot.inUse) continue;
            impl.m_characterVsCharacter->Remove(slot.character.GetPtr());
            slot.character = nullptr;
        }
        impl.m_characters.clear();
        impl.m_freeCharacterSlots.clear();
        impl.m_characterVsCharacter.reset();

        // Then our bodies, while the world that owns them still exists.
        JPH::BodyInterface& bodies = impl.Bodies();
        for (const std::uint32_t id : impl.m_bodies) {
            bodies.RemoveBody(JPH::BodyID(id));     // out of the simulation...
            bodies.DestroyBody(JPH::BodyID(id));    // ...then gone
        }
        impl.m_bodies.clear();
        impl.m_movedBodies.clear();

        // Reverse of Init. The layer tables go last because the world holds
        // references to them.
        impl.m_world.reset();
        impl.m_jobSystem.reset();       // joins the worker threads
        impl.m_tempAllocator.reset();
        impl.m_layerVsTree.reset();
        impl.m_layerToTree.reset();
        impl.m_layerPairs.reset();

        // Jolt's globals, so a later Init starts from a clean slate.
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        impl.m_initialised = false;
        CRU_INFO("Physics world stopped");

    }   // Physics::Shutdown()

    void Physics::OptimizeBroadPhase() {
        Impl& impl = *m_impl;
        if (impl.m_initialised) impl.m_world->OptimizeBroadPhase();
    }

    /*!
        \brief
        One tick. Jolt spreads the work over its worker threads and returns when
        all of it is done; then the bodies that moved are collected for the ECS.
    */
    void Physics::Step(float fixedDt) {

        Impl& impl = *m_impl;
        impl.m_movedBodies.clear();
        if (!impl.m_initialised || !(fixedDt > 0.0f)) return;

        const JPH::EPhysicsUpdateError errors =
            impl.m_world->Update(fixedDt, 1, impl.m_tempAllocator.get(), impl.m_jobSystem.get());

        if (errors != JPH::EPhysicsUpdateError::None)
            CRU_ERROR("Physics::Step: a buffer filled up and contacts were dropped - raise the limits in PhysicsSettings");

        // WHAT MOVED -------------------------------------------------------------- //
        // Only awake bodies can have moved. Jolt puts a body to sleep once it has
        // been still for a moment and drops it from this list, which is why a pile
        // of settled crates costs nothing to sync.
        JPH::BodyIDVector active;
        impl.m_world->GetActiveBodies(JPH::EBodyType::RigidBody, active);

        JPH::BodyInterface& bodies = impl.Bodies();
        for (const JPH::BodyID& id : active) {

            if (!impl.m_bodies.contains(id.GetIndexAndSequenceNumber())) continue;   // not one of ours

            JPH::RVec3 position;
            JPH::Quat  rotation;
            bodies.GetPositionAndRotation(id, position, rotation);

            Impl::Moved moved;
            moved.m_userData = bodies.GetUserData(id);
            moved.m_position = FromJolt(position);
            moved.m_rotation = FromJolt(rotation);
            impl.m_movedBodies.push_back(moved);
        }

    }   // Physics::Step()

    std::uint32_t Physics::GetMovedBodyCount() const {
        return static_cast<std::uint32_t>(m_impl->m_movedBodies.size());
    }

    bool Physics::GetMovedBody(std::uint32_t index, std::uint64_t& outUserData, Math::vec3& outPosition, Math::quat& outRotation) const {

        const Impl& impl = *m_impl;
        if (index >= impl.m_movedBodies.size()) return false;

        const Impl::Moved& moved = impl.m_movedBodies[index];
        outUserData = moved.m_userData;
        outPosition = moved.m_position;
        outRotation = moved.m_rotation;
        return true;

    }   // Physics::GetMovedBody()

}   // NAMESPACE CRUCIBLE

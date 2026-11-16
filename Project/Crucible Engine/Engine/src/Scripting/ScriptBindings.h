/*************************************************************************
* \file:         ScriptBindings.h
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          30 October 2026
* \version       1.0
* \brief         Registers the C++ functions that C# scripts can call
*                ("internal calls").
*
* \details
*    How a binding works:
*      1. C# declares an `extern` method marked
*         [MethodImpl(MethodImplOptions.InternalCall)] in ManagedScripts/Runtime.
*      2. A C++ function with matching parameters lives in Scripting/Bindings/.
*      3. mono_add_internal_call("Namespace.Class::Method", function) links
*         them. Each file in Bindings/ does this in its RegisterXxxBindings().
*
*    To add a binding (e.g. Transform):
*      - create Bindings/TransformBindings.cpp with RegisterTransformBindings()
*      - declare RegisterTransformBindings() below
*      - call it from RegisterAll() in ScriptBindings.cpp
*
*    No Mono types appear in this header.
*
**************************************************************************/
#pragma once

namespace Crucible
{
	class IScriptAnimatorBinding;
	class World;
	class PlayMode;
	class SceneManager;
	class AssetManager;
	struct UserSettings;
	class ECSPhysicsSystem;
	class Renderer;
	class Core;
	class ParticleSystem;
	struct EventBuses;
	class ResourceManager;

	namespace ScriptBindings
	{
		/*********************************************************************
		 \brief  Registers every internal call with Mono. Called once by
		         ScriptRuntime::Initialize, after the JIT starts and before any
		         user DLL is loaded.
		 *********************************************************************/
		void RegisterAll();

		/*********************************************************************
		 \brief  Disconnects every engine adapter (so C# calls safely return
		         "failed"). Called by ScriptRuntime::Shutdown.
		 *********************************************************************/
		void ClearAll();

		/*********************************************************************
		 \brief  Sets (or clears, with nullptr) the animation-system adapter used
		         by the C# Animator API. Non-owning; main thread only.
		         Called through ScriptRuntime::SetAnimatorBinding.
		 *********************************************************************/
		void SetAnimator(IScriptAnimatorBinding* binding);

		/*********************************************************************
		 \brief  Gives entity bindings (Transform, Entity, Prefab, ...) access to
		         the ECS, Play state, the open scene (entity fields are saved as
		         scene ids) and the assets (prefab fields are asset guids).
		         nullptr clears it. Non-owning; main thread only.
		         Called through ScriptRuntime::SetWorldBinding. Defined in
		         Bindings/EntityContext.cpp.
		 *********************************************************************/
		void SetWorld(World* world, PlayMode* play, SceneManager* scenes, AssetManager* assets);

		/*********************************************************************
		 \brief  Gives the C# Audio class the player's settings (bus volumes),
		         which ECSAudioSystem reads every frame. nullptr clears it.
		         Non-owning; main thread only. Called through
		         ScriptRuntime::SetUserSettingsBinding. Defined in
		         Bindings/AudioBindings.cpp.
		 *********************************************************************/
		void SetUserSettings(UserSettings* settings);

		/*********************************************************************
		 \brief  The player's settings given to SetUserSettings, or nullptr.
		         For other bindings that edit them (InputSettingsBindings.cpp).
		 *********************************************************************/
		UserSettings* GetUserSettings();

		/*********************************************************************
		 \brief  Gives C# Physics.Raycast the physics system (its entity queries).
		         nullptr clears it. Non-owning; main thread only. Called through
		         ScriptRuntime::SetPhysicsBinding. Defined in
		         Bindings/PhysicsBindings.cpp.
		 *********************************************************************/
		void SetPhysics(ECSPhysicsSystem* physics);

		/*********************************************************************
		 \brief  Gives the C# Camera class the renderer (game screen size, and
		         window -> game screen mapping for ScreenPointToRay). nullptr
		         clears it. Non-owning; main thread only. Called through
		         ScriptRuntime::SetRendererBinding. Defined in
		         Bindings/CameraBindings.cpp.
		 *********************************************************************/
		void SetRenderer(const Renderer* renderer);

		/*********************************************************************
		 \brief  Gives the C# Screen class the engine (window mode; editor or
		         game). nullptr clears it. Non-owning; main thread only. Called
		         through ScriptRuntime::SetCoreBinding. Defined in
		         Bindings/ScreenBindings.cpp.
		 *********************************************************************/
		void SetCore(Core* core);

		/*********************************************************************
		 \brief  Gives the C# MeshRenderer the resource manager (a model's
		         material slots). nullptr clears it. Non-owning; main thread
		         only. Defined in Bindings/MeshRendererBindings.cpp.
		 *********************************************************************/
		void SetResources(ResourceManager* resources);

		/*********************************************************************
		 \brief  Gives the C# ParticleEmitter class the particle system (Emit,
		         Restart, ParticleCount). nullptr clears it. Non-owning; main
		         thread only. Called through ScriptRuntime::SetParticleSystemBinding.
		         Defined in Bindings/ParticleBindings.cpp.
		 *********************************************************************/
		void SetParticleSystem(ParticleSystem* system);

		/*********************************************************************
		 \brief  Subscribes the C# UI classes to UIEvents (ButtonClicked,
		         SliderChanged, CustomAction) on the engine bus; nullptr
		         unsubscribes. Main thread only. Called through
		         ScriptRuntime::SetEventsBinding. Defined in Bindings/UIBindings.cpp.
		 *********************************************************************/
		void SetEvents(EventBuses* events);

		/*********************************************************************
		 \brief  Subscribes the scripts' OnCollision* / OnTrigger* callbacks to
		         PhysicsEvents on the engine bus; nullptr unsubscribes. Called by
		         SetEvents. Defined in Bindings/PhysicsBindings.cpp.
		 *********************************************************************/
		void SetPhysicsEvents(EventBuses* events);

		/*********************************************************************
		 \brief  Gives the C# Network class the engine's network session
		         (Core::GetNetwork) and subscribes the scripts' OnNetwork* /
		         OnPeer* callbacks to NetworkEvents on the network bus; nullptr
		         for either disconnects both. Non-owning; main thread only.
		         Called by ScriptRuntime::ConnectEngine. Defined in
		         Bindings/NetworkBindings.cpp.
		 *********************************************************************/
		void SetNetwork(Core* core, EventBuses* events);

		// --- Per-feature registration, called by RegisterAll() ---
		void RegisterLogBindings();      // Bindings/LogBindings.cpp      (C# Log)
		void RegisterAnimatorBindings(); // Bindings/AnimatorBindings.cpp (C# Animator)
		void RegisterTransformBindings(); // Bindings/TransformBindings.cpp (C# Transform)
		void RegisterInputBindings();     // Bindings/InputBindings.cpp     (C# Input)
		void RegisterEntityBindings();    // Bindings/EntityBindings.cpp    (C# Entity)
		void RegisterPrefabBindings();    // Bindings/PrefabBindings.cpp    (C# Prefab, Entity.Destroy)
		void RegisterAudioBindings();     // Bindings/AudioBindings.cpp     (C# AudioSource)
		void RegisterPhysicsBindings();   // Bindings/PhysicsBindings.cpp   (C# Physics, CharacterController)
		void RegisterTimeBindings();      // Bindings/TimeBindings.cpp      (C# Time)
		void RegisterHierarchyBindings(); // Bindings/HierarchyBindings.cpp (C# Transform world space, parent/children)
		void RegisterSceneBindings();     // Bindings/SceneBindings.cpp     (C# Scene)
		void RegisterCameraBindings();    // Bindings/CameraBindings.cpp    (C# Camera, CameraOrbit)
		void RegisterLightBindings();     // Bindings/LightBindings.cpp     (C# Light)
		void RegisterParticleBindings();  // Bindings/ParticleBindings.cpp  (C# ParticleEmitter)
		void RegisterUIBindings();        // Bindings/UIBindings.cpp        (C# UIText, UIImage, UISlider, UIButton, UIRect, UICanvas, UI)
		void RegisterEnableBindings();    // Bindings/EnableBindings.cpp    (C# SetEnabled<T>, SetActive, EntityScript.Enabled)
		void RegisterColliderBindings();  // Bindings/ColliderBindings.cpp  (C# Collider)
		void RegisterMeshRendererBindings(); // Bindings/MeshRendererBindings.cpp (C# MeshRenderer, MaterialParams, Material, Model)
		void RegisterPlayerControllerBindings(); // Bindings/PlayerControllerBindings.cpp (C# PlayerController)
		void RegisterInputSettingsBindings();  // Bindings/InputSettingsBindings.cpp (C# InputSettings: keybinds, look sensitivity)
		void RegisterScreenBindings();         // Bindings/ScreenBindings.cpp         (C# Screen: window mode)
		void RegisterNavigationBindings();     // Bindings/NavigationBindings.cpp     (C# FlowFieldAgent, FlowFieldSurface)
		void RegisterStateGraphBindings();     // Bindings/StateGraphBindings.cpp     (C# StateGraphAgent: variables, current state)
		void RegisterTagBindings();            // Bindings/TagBindings.cpp            (C# Entity.Tag, CompareTag, FindWithTag)
		void RegisterNetworkBindings();        // Bindings/NetworkBindings.cpp        (C# Network, Entity.NetworkId / Owner / IsMine)
	}
}

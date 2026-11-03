/*************************************************************************
* \file:         ScriptBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          30 October 2026
* \version       1.0
* \brief         Implementation - see ScriptBindings.h.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#if CRUCIBLE_WITH_MONO

namespace Crucible::ScriptBindings
{
	void RegisterAll()
	{
		RegisterLogBindings();
		RegisterAnimatorBindings();
		RegisterTransformBindings();
		RegisterInputBindings();
		RegisterEntityBindings();
		RegisterPrefabBindings();
		RegisterAudioBindings();
		RegisterPhysicsBindings();
		RegisterTimeBindings();
		RegisterHierarchyBindings();
		RegisterSceneBindings();
		RegisterCameraBindings();
		RegisterLightBindings();
		RegisterParticleBindings();
		RegisterUIBindings();
		RegisterEnableBindings();
		RegisterColliderBindings();
		RegisterMeshRendererBindings();
		RegisterPlayerControllerBindings();
		RegisterInputSettingsBindings();
		RegisterScreenBindings();
	}

	void ClearAll()
	{
		SetAnimator(nullptr);
		SetWorld(nullptr, nullptr, nullptr, nullptr);
		SetUserSettings(nullptr);
		SetPhysics(nullptr);
		SetRenderer(nullptr);
		SetParticleSystem(nullptr);
		SetEvents(nullptr);
		SetCore(nullptr);
		SetResources(nullptr);
		// Clear new adapters here.
	}
}
#endif

/*************************************************************************
* \file:         ScriptBindings.cpp
* \author:       <author>
* \par:          Email: <email>
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
		// Clear new adapters here.
	}
}
#endif

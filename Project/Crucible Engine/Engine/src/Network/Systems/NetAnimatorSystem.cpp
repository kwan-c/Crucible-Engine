/*************************************************************************
* \file:         NetAnimatorSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                Replicated Animator parameters in the engine loop (see the
*                header).
*
* \history
*    -v1.0       Networking C# Phase 7
*
**************************************************************************/
#include "Network/Systems/NetAnimatorSystem.h"

#include "Animation/AnimatedModelSystem.h"
#include "ECS/World.h"
#include "Network/NetworkSession.h"

namespace Crucible
{
	NetAnimatorSystem::NetAnimatorSystem(NetworkSession& session, IScriptAnimatorBinding& animation, World& world, AnimatorResolver resolver)
		: m_session(session), m_animation(animation), m_resolver(resolver),
		  m_binding(animation, world, std::move(resolver), [&session] { return session.Role(); })
	{
	}

	void NetAnimatorSystem::Describe(SystemDesc& desc) const
	{
		// Before the animation system consumes this frame's parameter writes.
		desc.RunBeforeIfPresent<AnimatedModelSystem>()
			.Affinity(SystemAffinity::MainThread);   // reads the session
	}

	void NetAnimatorSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands)
	{
		if (!m_session.IsClient() || m_session.State() != SessionState::Running)
		{
			m_warned.clear();   // the next session warns again
			return;
		}
		ApplyNetAnimators(world, m_resolver, m_animation, m_layout, m_warned);
	}
}

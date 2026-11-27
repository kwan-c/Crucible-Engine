/*************************************************************************
* \file:         NetAnimatorSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                Replicated Animator parameters in the engine loop
*                (Network/NetAnimator.h): owns the NetAnimatorBinding that C#
*                Animator calls go through, and on a client hands the host's
*                values (NetAnimator) to this machine's animation system
*                every frame (ApplyNetAnimators).
*
* \details
*                PreRender, before AnimatedModelSystem, so the values are in
*                the animator before this frame's pose is evaluated. Host
*                and offline: Update does nothing (the host's scripts write
*                its animators directly, and Binding() records them).
*
* \history
*    -v1.0       Networking C# Phase 7
*
**************************************************************************/
#pragma once

#include <unordered_set>

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Network/NetAnimator.h"

namespace Crucible
{
	class NetworkSession;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // std members of an exported class: used only inside Engine.dll
#endif

	class ENGINE_API NetAnimatorSystem final : public IECSSystem
	{
	public:
		/// `animation` is the animation system: where every value finally goes.
		NetAnimatorSystem(NetworkSession& session, IScriptAnimatorBinding& animation, World& world, AnimatorResolver resolver);

		SystemStage Stage() const override { return SystemStage::PreRender; }
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, CommandBuffer& commands) override;

		/// What C# Animator should call (ScriptEngineConnection::animator).
		IScriptAnimatorBinding& Binding() noexcept { return m_binding; }

	private:
		NetworkSession&                   m_session;
		IScriptAnimatorBinding&           m_animation;
		AnimatorResolver                  m_resolver;
		NetAnimatorBinding                m_binding;
		NetAnimatorLayout                 m_layout;   // scratch
		std::unordered_set<std::uint64_t> m_warned;
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

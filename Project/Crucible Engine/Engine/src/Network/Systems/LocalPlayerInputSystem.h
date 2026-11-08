/*************************************************************************
* \file:         LocalPlayerInputSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          5 November 2026
* \version       1.1
* \brief
*                The sampling half of the player controller: keyboard,
*                mouse and gamepad of THIS machine in, PlayerInput of the
*                locally controlled player(s) out. Also the local
*                presentation that goes with it: the third-person camera
*                pivot.
*
* \details
*                Split out of ECSPlayerControllerSystem (Hugo's), whose input
*                code it carries over unchanged. The controller now only applies
*                PlayerInput, so the same applying code serves the local
*                player (offline, and the host's own mech) and, in a later
*                phase, remote players
*                whose PlayerInput comes from their network commands.
*
*                Each Update (PreUpdate, after the UI's menu and input):
*                  1. (no cursor lock: scripts ask for it with
*                     Input.SetMouseLocked; mouse-look below needs it)
*                  2. while Playing, every locally controlled CameraOrbit
*                     pivot turns with the mouse and the right stick, exactly
*                     as before; its yaw is what the mech will face
*                  3. the keybinds and the left stick give a walk direction
*                     relative to the main camera, flat on the ground
*                  4. every locally controlled player's PlayerInput gets that
*                     direction, the yaw, and a latched jump - or NEUTRAL
*                     input (no walk, no jump; yaw kept) when it may not
*                     sample (see MaySample: Paused, or the in-game menu
*                     open) - and its sampleSerial moves on
*
*                Not SimulationOnly: while
*                Paused it still runs, writing neutral input, so a held key
*                is never left standing in PlayerInput (and later, never
*                sent to the host). In Edit and Paused it does not touch the
*                camera or the cursor.
*
*                Locally controlled: offline (or an entity without a
*                NetworkIdentity) every player is; in a session, only one
*                whose NetworkIdentity owner is this machine's peer.
*
*                PlayerInput is runtime-only (never saved), so every entity
*                with a PlayerController gets one on first sight (switched
*                off controllers too: on a client the controller is a
*                server-only component).
*
* \history
*    -v1.0       sampling split from ECSPlayerControllerSystem
*    -v1.1       no cursor lock on Play start: scripts lock it with
*                Input.SetMouseLocked
*
**************************************************************************/
#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "ECS/Systems/IECSSystems.h"
#include "ECS/Systems/Query.h"
#include "EngineExport.h"
#include "Player/PlayerComponents.h"

namespace Crucible
{
	struct InputSettings;
	class NetworkSession;
	class PlayMode;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members of an exported class: used only inside Engine.dll
#endif
	class ENGINE_API LocalPlayerInputSystem final : public IECSSystem
	{
	public:
		/// True while something other than gameplay owns this machine's input (Core passes the
		/// in-game menu, UI::IsMenuOpen). Empty: nothing does.
		using InputOwnedElsewhere = std::function<bool()>;

		/// settings: the player's sensitivity, invert Y and keybinds (Core's UserSettings).
		/// session: null, or Core's (who is locally controlled). All outlive this system.
		LocalPlayerInputSystem(const InputSettings& settings, const NetworkSession* session, const PlayMode& play,
			InputOwnedElsewhere ownedElsewhere = {})
			: m_settings(settings), m_session(session), m_play(play), m_ownedElsewhere(std::move(ownedElsewhere)) {}
		LocalPlayerInputSystem(const LocalPlayerInputSystem&) = delete;
		LocalPlayerInputSystem& operator=(const LocalPlayerInputSystem&) = delete;

		SystemStage Stage() const override { return SystemStage::PreUpdate; }
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, CommandBuffer& commands) override;

		/// THE place that decides whether this machine's input reaches its players this frame.
		/// While false they get neutral input: no walk, no jump (yaw kept, so nothing snaps).
		/// Today: Playing, and nothing else owning the input (the constructor's rule: Core
		/// passes the in-game menu). Window focus needs no rule here: Input clears held keys and
		/// ignores the gamepad while the window is in the background. To let the menu stop
		/// movement no more, pass no rule from Core::RegisterEngineSystems.
		bool MaySample() const;

	private:
		bool IsLocallyControlled(const World& world, Entity e) const;

		const InputSettings&  m_settings;
		const NetworkSession* m_session;
		const PlayMode&       m_play;
		InputOwnedElsewhere   m_ownedElsewhere;

		std::optional<Query<const PlayerController>> m_missingInput;   // players without a PlayerInput yet
		std::vector<std::pair<Entity, float>>         m_yaws;           // this frame: mech -> its pivot's yaw
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

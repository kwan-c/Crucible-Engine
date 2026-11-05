/*************************************************************************
* \file:         LocalPlayerInputSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          5 November 2026
* \version       1.1
* \brief
*                LocalPlayerInputSystem: this machine's input into its
*                players' PlayerInput, plus the cursor lock and the camera
*                pivot. See the header for what happens each frame.
*
* \details
*                The cursor, camera-pivot, keys and camera blocks are Hugo
*                Low Ren Hao's code from ECSPlayerControllerSystem v1.7 (keybinds
*                by Tan Chun Kwang Xavier), moved here unchanged except where
*                a comment says "Split:".
*
* \history
*    -v1.0       sampling split from ECSPlayerControllerSystem (PR 10)
*    -v1.1       on a client, movement is sampled although the proxy's
*                controller is switched off (the host applies it) (PR 11)
*
**************************************************************************/
#include "Network/Systems/LocalPlayerInputSystem.h"

// STL
#include <algorithm>
#include <cmath>

// Crucible engine
#include "Configuration/UserSettings.h"   // InputSettings
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Input/InputSystem.h"
#include "Math/Math.h"
#include "Network/NetComponents.h"
#include "Network/NetworkSession.h"
#include "Systems/PlayMode.h"
#include "UI/UISystem.h"                  // UIMenuSystem, UIInputSystem (order)

namespace Crucible
{
	namespace
	{
		// The rotation of the frame an entity's Transform is relative to: its parent's world
		// rotation (sockets included, via World::TryGetParentFrame). Exactly the identity for
		// a root. (A copy of ECSPlayerControllerSystem's helper, which still uses its own.)
		Math::quat FrameRotation(const World& world, Entity entity)
		{
			const Math::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
			Math::mat4 frame(1.0f);
			if (!world.TryGetParentFrame(entity, frame) || frame == Math::mat4(1.0f)) return identity;
			Math::vec3 translation, scale;
			Math::quat rotation;
			return Detail::DecomposeTRS(frame, translation, rotation, scale) ? rotation : identity;
		}
	}

	void LocalPlayerInputSystem::Describe(SystemDesc& desc) const
	{
		// Not SimulationOnly (issue 5): runs while Paused too, to write neutral input. After the
		// menu (this frame's open state) and the UI (it gets the mouse first), as before.
		desc.RunAfterIfPresent<UIMenuSystem>()
			.RunAfterIfPresent<UIInputSystem>()
			.Affinity(SystemAffinity::MainThread);   // touches Input (the cursor)
	}

	bool LocalPlayerInputSystem::MaySample() const
	{
		return m_play.State() == PlayState::Playing && !(m_ownedElsewhere && m_ownedElsewhere());
	}

	bool LocalPlayerInputSystem::IsLocallyControlled(const World& world, Entity e) const
	{
		const NetworkIdentity* identity = world.Read<NetworkIdentity>(e);
		if (identity == nullptr || m_session == nullptr || !m_session->IsActive()) return true;   // offline: every player
		return identity->owner == m_session->LocalPeer();
	}

	void LocalPlayerInputSystem::Update(World& world, CommandBuffer& commands)
	{
		const bool playing = m_play.State() == PlayState::Playing;
		const bool sampling = MaySample();

		// THE CURSOR -------------------------------------------------------------- //
		// Play or Resume has just begun: hide and capture the cursor, the way a Unity
		// game sets Cursor.lockState at start. Only on that first frame - after Esc
		// frees the cursor, clicking the Game view is what takes it back (the editor's
		// job), not this.
		// Split: this system also runs while Paused, so "just began" is the play state
		// turning to Playing, not the controller's old gap in the World's tick.
		if (sampling && !m_wasPlaying) Input::SetMouseLocked(true);
		m_wasPlaying = playing;

		m_yaws.clear();
		Math::vec3 direction(0.0f);
		bool       jump = false;

		// Split: Edit and Paused leave the camera, keyboard and mouse to the editor, as when this
		// code only ran while Playing; the players just get neutral input below.
		if (playing)
		{
			// THE CAMERA PIVOT --------------------------------------------------------- //
			// The mouse only turns the camera while the cursor is captured. A free
			// cursor is on its way to the editor's Pause button or the Inspector, and
			// the view spinning as it travels there would be maddening.
			// GetMouseDelta is a distance in pixels, so it is NOT multiplied by dt.
			// Split: menu ownership blocks look input while keeping pivot compensation active.
			Math::vec2 mouse = sampling && Input::IsMouseLocked() ? Input::GetMouseDelta() : Math::vec2(0.0f);

			// The right stick turns it too, and not only while the cursor is captured: a pad
			// cannot wander off to the Inspector the way a free mouse does. A stick is a
			// POSITION, not a distance, so unlike the mouse it is a speed - times dt.
			Math::vec2  stick = sampling ? Input::GetGamepadRightStick() : Math::vec2(0.0f);
			const float dt    = world.DeltaTime();

			// The PLAYER's settings on top of the designer's feel (CameraOrbit's m_sensitivity
			// and m_stickSpeed): multipliers, so 1 is "as designed". Applied to the input here,
			// once, rather than to the component, so a saved setting never overwrites what the
			// designer tuned. Read every frame, so an options menu takes effect at once.
			const float flipY = m_settings.invertY ? -1.0f : 1.0f;
			mouse = Math::vec2(mouse.x, mouse.y * flipY) * m_settings.mouseSensitivity;
			stick = Math::vec2(stick.x, stick.y * flipY) * m_settings.stickSensitivity;

			world.CreateQuery<CameraOrbit, const Transform>()
				.ForEach([&](Entity pivot, CameraOrbit& orbit, const Transform& transform) {

					// Split: only this machine's own players' cameras. Another player's pivot (a
					// remote player's camera rig) is not ours to turn.
					const Entity mech = world.GetParent(pivot);
					if (!IsLocallyControlled(world, mech)) return;

					// Mouse left (x < 0) turns the view left, which is + yaw around Y.
					// Mouse down (y > 0, pixels count downwards) tilts it down, which
					// is + pitch. Neither axis is inverted - the common default.
					orbit.m_yaw   -= mouse.x * orbit.m_sensitivity;
					orbit.m_pitch += mouse.y * orbit.m_sensitivity;

					// Stick right turns the view right (- yaw), like the mouse. Stick UP looks
					// up, which lowers the camera: - pitch. (The mouse's y counts downward in
					// pixels; the stick's y is up-positive - Input flipped GLFW's for us.)
					orbit.m_yaw   -= stick.x * orbit.m_stickSpeed * dt;
					orbit.m_pitch -= stick.y * orbit.m_stickSpeed * dt;

					// Yaw goes round and round; wrapping it stops a long session
					// growing it into the millions, where a float has no precision left.
					orbit.m_yaw = std::fmod(orbit.m_yaw, 360.0f);

					// Deliberately NOT std::clamp: clamp is undefined if a designer types
					// the limits the wrong way round, and in Debug it asserts mid-Play.
					// max(min(...)) is always defined; swapped limits just pin the
					// camera to m_minPitch until the typo is fixed.
					orbit.m_pitch = std::max(orbit.m_minPitch, std::min(orbit.m_pitch, orbit.m_maxPitch));

					// Yaw around the world's up, then pitch around the pivot's own
					// sideways axis - the order that keeps the horizon level. The
					// other order tilts the horizon as soon as both are non-zero.
					// Pitch is negated: turning around +X by a positive angle tips the
					// view UP, and + pitch means looking down on the mech.
					// This is the rotation the camera should end up with in the WORLD.
					const Math::quat yawRotation   = glm::angleAxis(glm::radians(orbit.m_yaw),    Math::vec3(0.0f, 1.0f, 0.0f));
					const Math::quat pitchRotation = glm::angleAxis(glm::radians(-orbit.m_pitch), Math::vec3(1.0f, 0.0f, 0.0f));
					const Math::quat worldRotation = yawRotation * pitchRotation;

					// THE MECH. The pivot is a child of the mech, so whatever the mech's
					// rotation is gets added on top of the pivot's own. Turning the mech
					// to the camera AND giving the pivot the full yaw would turn the
					// camera twice - and every frame the mech chases it, it runs away
					// further. So the mech's rotation is cancelled out below, whatever it
					// is: toggle on or off, flipped mid-Play or not, the view never jumps.
					// The mech's WORLD rotation is what has to be cancelled: its own
					// Transform rotation on top of whatever it hangs under (a rotated
					// group, a moving platform). For the usual root mech that frame is
					// the identity, so it is just its Transform rotation.
					Math::quat parentRotation = Math::quat(1.0f, 0.0f, 0.0f, 0.0f);   // no parent: nothing to cancel
					if (const Transform* mechTransform = world.Read<Transform>(mech)) {
						const Math::quat mechFrame = FrameRotation(world, mech);   // what the mech's Transform is relative to
						parentRotation = mechFrame * mechTransform->rotation;

						// Toggle on: the mech turns to the camera's yaw. Yaw only - a
						// mech that pitched with the camera would lie down to look at the
						// floor.
						// Split: turning the mech is movement, so ECSPlayerControllerSystem does
						// it, from PlayerInput::yawDegrees (handed over below). The camera only
						// needs to know that the mech WILL face this yaw this frame.
						const PlayerController* player = world.Read<PlayerController>(mech);
						if (player != nullptr) m_yaws.emplace_back(mech, orbit.m_yaw);
						if (player != nullptr && player->m_faceCamera) parentRotation = yawRotation;
					}

					// Undo the parent's turn: parent x local = world, so local =
					// inverse(parent) x world. Facing the camera, that leaves just the pitch.
					const Math::quat rotation = glm::inverse(parentRotation) * worldRotation;

					// Written every frame, not only when the mouse moves, so the saved
					// angles take effect on the first frame of Play and an Inspector
					// edit during Play shows up at once. But only when it differs:
					// a mutable GetComponent stamps the Transform as changed, and an
					// untouched camera would otherwise re-stamp itself 60 times a second.
					if (rotation != transform.rotation) {
						if (Transform* writable = world.GetComponent<Transform>(pivot)) writable->rotation = rotation;
					}
				});

			// THE KEYS ---------------------------------------------------------------- //
			// The player's keybinds (UserSettings.json, "input/keys"; W A S D / Space by
			// default - an options menu or a script can change them).
			// Each axis is -1, 0 or +1. Holding both left and right cancels out to 0, which
			// is what Unity's Input.GetAxisRaw("Horizontal") does too.
			float right   = 0.0f;
			float forward = 0.0f;
			if (Input::IsDown(m_settings.rightKey))   right   += 1.0f;
			if (Input::IsDown(m_settings.leftKey))    right   -= 1.0f;
			if (Input::IsDown(m_settings.forwardKey)) forward += 1.0f;
			if (Input::IsDown(m_settings.backKey))    forward -= 1.0f;

			// The left stick adds to the keys rather than replacing them, so either works
			// and neither has to be "switched to". It is analog: half tilt walks at half
			// speed (the length clamp below only shrinks values OVER 1).
			const Math::vec2 walkStick = Input::GetGamepadLeftStick();
			right   += walkStick.x;
			forward += walkStick.y;

			// IsPressed, not IsDown: one jump per press. Holding Space would
			// otherwise jump again the moment the mech lands - a pogo stick.
			jump = Input::IsPressed(m_settings.jumpKey) || Input::IsPressed(m_settings.jumpButton);

			// THE CAMERA -------------------------------------------------------------- //
			// Unity's Camera.main: the enabled camera with the highest priority, the
			// first one found on a tie. The SAME rule RenderExtractSystem uses to pick
			// what the Game view shows - if one changes, change the other, or W walks
			// relative to a camera nobody is looking through.
			//
			// WorldTransform, not Transform: the camera is usually a child of the
			// mech, and its Transform is then only its offset from the mech. It is
			// last frame's (HierarchyTransformSystem fills it in PreRender, after
			// this), which is one frame of camera turn - nothing anyone can feel.
			bool       found        = false;
			int        bestPriority = 0;
			Math::vec3 cameraRight  = Math::vec3(1.0f, 0.0f, 0.0f);   // no camera: W is world -Z, D is world +X

			world.CreateQuery<const WorldTransform, const CameraComponent>()
				.ForEach([&](Entity, const WorldTransform& worldTransform, const CameraComponent& camera) {

					if (!camera.enabled || !worldTransform.valid) return;
					if (found && camera.priority <= bestPriority) return;

					// Column 0 of a world matrix is the entity's local +X axis in
					// world space: the camera's right. Flattened, because a tilted
					// or rolled camera would otherwise steer the mech into the floor.
					const Math::vec3 flat(worldTransform.matrix[0].x, 0.0f, worldTransform.matrix[0].z);
					if (glm::dot(flat, flat) < 1.0e-6f) return;     // rolled onto its side: no usable heading

					found        = true;
					bestPriority = camera.priority;
					cameraRight  = glm::normalize(flat);
				});

			// Forward comes from right, not from the camera's own forward. A camera
			// looking straight down at the mech has a forward of (0, -1, 0), which
			// flattens to nothing; its right is still perfectly level. Up x right is
			// the direction 90 degrees to the left of right, along the ground -
			// with a right-handed, Y-up world that is exactly "away from the camera".
			const Math::vec3 cameraForward = glm::cross(Math::vec3(0.0f, 1.0f, 0.0f), cameraRight);

			// W+D gives a vector about 1.41 long, so diagonal walking would be 41%
			// faster. Shrink it back to 1. Only when over 1, so a gamepad stick
			// pushed halfway can still mean "walk slowly" later.
			direction = cameraRight * right + cameraForward * forward;
			if (glm::dot(direction, direction) > 1.0f) direction = glm::normalize(direction);
		}

		// THE PLAYERS' INPUT ------------------------------------------------------------ //
		// Split: what the controller used to hand straight to SimpleMove and Jump goes into
		// PlayerInput instead, and ECSPlayerControllerSystem applies it in Update.
		// The last pivot found wins, as when each pivot turned its mech in turn.
		const auto yawOf = [&](Entity mech, float& yaw) {
			for (auto it = m_yaws.rbegin(); it != m_yaws.rend(); ++it) if (it->first == mech) { yaw = it->second; return true; }
			return false;
		};
		// On a client the host's controller decides: here a proxy's PlayerController and
		// CharacterController are switched off (server-only), so only sampling matters, and the
		// host applies what NetInputSystem sends it.
		const bool onClient = m_session != nullptr && m_session->IsClient();
		const auto canAcceptMovement = [&](Entity entity) {
			return sampling && (onClient
				|| (world.IsComponentActive<PlayerController>(entity) && world.IsComponentActive<CharacterController>(entity)));
		};

		// Players without a PlayerInput get one, already filled in, at the end of this stage
		// (before the controller's Update), so the first frame of Play moves like every other.
		if (!m_missingInput) m_missingInput.emplace(world.BuildQuery<const PlayerController>().Exclude<PlayerInput>().IncludeDisabled().Build());
		m_missingInput->ForEach([&](Entity e, const PlayerController&) {
			PlayerInput input;
			if (IsLocallyControlled(world, e))
			{
				const bool acceptsMovement = canAcceptMovement(e);
				input.moveDirection = acceptsMovement ? direction : Math::vec3(0.0f);
				input.jump = acceptsMovement && jump;
				input.hasYaw = yawOf(e, input.yawDegrees);
				input.sampleSerial = 1;
			}
			commands.AddComponent(e, input);
		});

		world.CreateQuery<PlayerInput>().ForEach([&](Entity e, PlayerInput& input) {
			if (!IsLocallyControlled(world, e)) return;   // a remote player's: its commands fill it in
			const bool acceptsMovement = canAcceptMovement(e);
			input.moveDirection = acceptsMovement ? direction : Math::vec3(0.0f);
			// Latched until the controller uses it, so a press is never lost between the frame it
			// happens and the one that applies it; dropped when the controller cannot accept input.
			input.jump = acceptsMovement && (input.jump || jump);
			if (sampling)
			{
				float yaw = input.yawDegrees;
				input.hasYaw = yawOf(e, yaw);   // clear stale facing when its camera disappears
				if (input.hasYaw) input.yawDegrees = yaw;
			}   // preserve facing during pause/menu handling
			++input.sampleSerial;   // the sampler ran this frame (issue 5)
		});
	}
}

#pragma once
/*!
* @file			NetworkAuthoringInspector.h
* @author		Abhishek Akilandeswari Patchamuthu
* @par			a.patchamuthu@digipen.edu
* @date			6 November 2026
*
* @brief		The Inspector's networking policy table for NetworkObjectAuthoring (Networking
*				Phase 14.3).
*
* One row per configurable component of the object (its root and everything below it), each
* with one choice per column:
*	Network state   Local (keeps the value its scene or prefab loaded, on every machine) or
*	                Replicated, with a serializer variant (Transform: Full or YawOnly)
*	Runs on         Everywhere, Host only (switched off on clients: AI, scripts, physics) or
*	                Owner only (switched off except on the owning machine: camera rig, HUD)
*	Sync on/off     the root component's on/off state is replicated (EnabledMask)
* Only valid choices can be picked; the others are greyed out with the reason as a tooltip. The
* EnabledMask serializer is added and removed with the first and last "Sync on/off" by itself.
* Below the table: the scheduling values (send priority, critical), scene placement, spawn per
* player, and every problem a session start would refuse (CheckAuthoring), or "valid".
*
* The table only edits the component (an ordinary undo step through the Inspector); it never
* changes any other component, and nothing it does reaches a saved file until the scene or
* prefab is saved. Settings naming a component the object no longer has are listed with a
* Remove button.
*
* Called from the Inspector (ImGuiPanel_Inspector, DrawComponentExtras and AddBlockedReason).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ECS/Core/ECSCore.h"

namespace Crucible
{
	class ComponentRegistry;
	class ReplicationRegistry;
	class World;
	struct NetworkObjectAuthoring;

	namespace NetworkAuthoringInspector
	{
		/// Draws the table for `authoring` on `entity`. True if it changed the component.
		bool Draw(World& world, Entity entity, NetworkObjectAuthoring& authoring,
			const ComponentRegistry& components, const ReplicationRegistry& serializers);

		/// Why NetworkObjectAuthoring cannot be added to `entity` (an entity above or below it has
		/// one: a network object cannot contain another), or nullptr if it can.
		const char* AddBlockedReason(const World& world, Entity entity);
	}
}

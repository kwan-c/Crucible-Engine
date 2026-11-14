/*************************************************************************
* \file:         TagEditor.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 November 2026
* \version       1.1
* \brief
*                The editor side of entity tags: the Inspector's Tag
*                dropdown, the "Add New Tag..." dialog, and the Tag Manager
*                window (create, rename, delete project tags).
*
* \details
*                Tags themselves: EntityTag (ECS/Components/Components.h),
*                World's tag functions (ECS/World.h) and the project's tag
*                list, TagRegistry (ECS/TagRegistry.h), saved in
*                Assets/Config/Tags.json after every change here.
*
*                Undo: assigning a tag is an ordinary Inspector edit (an
*                EntityStateCommand). Changes to the tag LIST are undo steps
*                too (TagListCommand), so undoing "create tag, then assign
*                it" takes the assignment back first, then the tag.
*
*                Rename and Delete first scan the whole project for the tag
*                (Tags/TagReferences.h: the open scene, every scene and
*                prefab, saved tag settings, script string literals):
*                  - used nowhere: the list alone changes (an undo step).
*                  - used: Rename renames it everywhere; Delete asks what
*                    the uses become - "Replace with" another tag, or "Reset
*                    to Untagged" - and nothing changes until that is
*                    confirmed. Files are rewritten with backups and
*                    rollback (TagReferences::Retag); not an undo step, and
*                    the undo history is cleared afterwards.
*                  - blocked, with the reasons shown, when the scan is
*                    incomplete (unreadable / invalid files), a file to
*                    change is read-only, or a saved tag setting names it.
*                  - "Untagged" is protected; nothing is offered for it.
*                  - Not during Play (Stop would undo the scene's part).
*                The manager also lists undefined tags used in the open
*                scene, with "Add to project" to define them again.
*
* \history
*    -v1.0       Inspector tag field, New Tag dialog, Tag Manager,
*                TagListCommand
*    -v1.1       project-wide Rename / Delete (Replace with, Reset to
*                Untagged) through TagReferences; TagListCommand only for
*                list changes nothing depends on
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <string>

#include "ECS/Core/ECSCore.h"   // Entity
#include "Undo/UndoHistory.h"   // EditorCommand

namespace Crucible
{
	class World;
	class SceneManager;

	/*************************************************************************
	 \brief  One change to the project's tag list (TagRegistry) that no
			 entity or file depends on - creating a tag, or renaming /
			 deleting an unused one - as an undo step. Every Undo/Redo saves
			 Assets/Config/Tags.json again.
	 \note   Project-wide renames and deletes (TagReferences::Retag) rewrite
			 files on disk and are not undo steps.
	 *************************************************************************/
	class TagListCommand final : public EditorCommand
	{
	public:
		enum class Kind { Add, Remove, Rename };

		/// tag: the tag added / removed / renamed; renamedTo: Rename only;
		/// index: where it sits in the list (Add / Remove), to put it back there.
		TagListCommand(Kind kind, std::string tag, std::string renamedTo, std::size_t index, std::string label)
			: m_kind(kind), m_tag(std::move(tag)), m_renamedTo(std::move(renamedTo)), m_index(index), m_label(std::move(label))
		{
		}

		void Undo(UndoHistory& history) override;
		void Redo(UndoHistory& history) override;
		EntityKey          Target() const override { return kNullEntity; }   // no entity: selection is left alone
		const std::string& Label()  const override { return m_label; }

	private:
		void Apply(bool forward);

		Kind        m_kind;
		std::string m_tag;
		std::string m_renamedTo;
		std::size_t m_index;
		std::string m_label;
	};

	namespace TagEditor
	{
		/*********************************************************************
		 \brief  What the user chose in the Inspector's tag field this frame.
		 \details create: define `tag` first (call CreateTag), then, if
				  assign, give it to the entity. assign alone: an existing tag.
		 *********************************************************************/
		struct TagPick
		{
			bool        assign = false;
			bool        create = false;
			std::string tag;
		};

		/*********************************************************************
		 \brief  Draws "Tag [Obstacle v]" (every project tag, then "Add New
				 Tag..." and "Manage Tags..."), the New Tag dialog, and a
				 warning under it if the entity's tag is undefined.
		 \return The pick, for the Inspector to apply as an undoable edit.
				 Nothing is changed here.
		 *********************************************************************/
		TagPick DrawTagField(World const& world, Entity entity);

		/*********************************************************************
		 \brief  Defines a new project tag: adds it, saves Tags.json and
				 records the undo step.
		 \return false (nothing changed, *error set) if the name is refused.
		 *********************************************************************/
		bool CreateTag(std::string const& tag, std::string* error = nullptr);

		/// Shows the Tag Manager window.
		void OpenManager();

		/*********************************************************************
		 \brief  Draws the Tag Manager window while it is open. Call every
				 frame, outside any other window (the Inspector does).
		 *********************************************************************/
		void DrawManager(World& world, SceneManager& scenes);
	}
}

#pragma once
#include "Animation/AnimatorController.h"
#include <optional>

namespace Crucible {
    struct AnimatorGraphPoint { float x = 0, y = 0; bool operator==(const AnimatorGraphPoint&) const = default; };
    // Editor-only asset document. Scene/entity undo and runtime state never live here.
    class AnimatorDocument {
    public:
        struct Snapshot {
            AnimatorController controller;
            std::vector<AnimatorGraphPoint> positions;
            AnimatorGraphPoint anyState{0, -140};
            uint64_t previewModel = 0;
        };
        AnimatorDocument() { New(); }
        void New();
        bool Load(const std::string& json, std::string& error);
        bool Serialize(std::string& json, std::string& error, std::optional<uint64_t> sourceModel = std::nullopt) const;
        void MarkSaved(std::optional<uint64_t> sourceModel = std::nullopt);
        void DiscardChanges();
        bool Dirty() const;
        bool RuntimeMatchesSaved() const;
        Snapshot& Data() { return m_data; }
        const Snapshot& Data() const { return m_data; }
        void BeginEdit();
        void EndEdit(bool keepOpen = false);
        void Undo();
        void Redo();
        bool CanUndo() const;
        bool CanRedo() const { return !m_redo.empty(); }
        uint32_t AddState(AnimatorGraphPoint position);
        uint32_t DuplicateState(uint32_t state);
        void RemoveState(uint32_t state);
        void RemoveParameter(uint32_t parameter);
        void ChangeParameterType(uint32_t parameter, AnimatorParameterType type);
        uint32_t AddTransition(uint32_t from, uint32_t to);
        static bool SameRuntime(const AnimatorController&, const AnimatorController&);
    private:
        static bool Same(const Snapshot&, const Snapshot&);
        Snapshot m_data, m_saved;
        std::optional<Snapshot> m_before;
        std::vector<Snapshot> m_undo, m_redo;
    };
}

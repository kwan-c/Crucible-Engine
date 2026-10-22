#pragma once
#include "ImGui/ImGuiManager.h"
#include "Animation/AnimatorDocument.h"
#include <functional>

namespace Crucible {
    class AssetManager;
    class AnimatorPanel final : public EditorPanel {
    public:
        explicit AnimatorPanel(Core& core) : m_core(core) {}
        void Init() override;
        void Update(float) override;
        void Show() { m_visible = true; m_makeFocus = true; }
        void RequestOpen(uint64_t asset);
        void RequestNew();
        bool Save();
        bool SaveAs(const std::string& virtualPath);
        // Menu's existing quit guard calls this after the scene's own guard.
        bool GuardExit(std::function<void()> continuation);
        bool HasKeyboardFocus() const { return m_focused && m_visible; }
        bool IsUnsaved() const { return m_hasDocument && (!m_asset || m_document.Dirty()); }
        const AnimatorDocument& Document() const { return m_document; }
        uint64_t Asset() const { return m_asset; }
    private:
        enum class Pending { None, Open, New, Reload, Close, Exit };
        void Request(Pending, uint64_t asset = 0);
        void ApplyPending();
        void DiscardDraft();
        bool Load(uint64_t asset);
        void DrawToolbar(bool playing);
        void DrawParameters(bool readOnly);
        void DrawCanvas(bool readOnly);
        void DrawDetails(bool readOnly);
        void DrawLive();
        void DrawDialogs(bool playing);
        void ResetSelection();
        void DeleteSelection();
        uint64_t SelectedClipSourceModel() const;
        uint64_t ClipSourceModel();
        bool ValidateForSave(std::string& json, uint64_t& sourceModel);
        uint32_t LiveCurrent() const;
        uint32_t LiveNext() const;
        Core& m_core;
        AnimatorDocument m_document;
        uint64_t m_asset = 0;
        uint64_t m_clipSourceContext = 0; // last matching entity's model; selection is not an authored edit
        std::string m_savedText, m_error;
        bool m_hasDocument = false, m_visible = false, m_focused = false;
        Pending m_pending = Pending::None;
        uint64_t m_pendingAsset = 0;
        std::function<void()> m_exitContinuation;
        bool m_openGuard = false, m_openSaveAs = false, m_continueAfterSave = false;
        char m_savePath[512] = "Animation/New Controller.animcontroller";
        int m_state = -1, m_transition = -1, m_parameter = -1;
        bool m_anySelected = false, m_connecting = false;
        uint32_t m_connectFrom = InvalidModelIndex;
        ImVec2 m_pan{45, 175};
        float m_zoom = 1.0f;
        bool m_fitRequested = false;
        int m_dragNode = -2; // -2: none; -1: Any State
        AnimatorGraphPoint m_contextPosition;
    };
}

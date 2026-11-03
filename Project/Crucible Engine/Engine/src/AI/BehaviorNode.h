/*************************************************************************
* \file:         BehaviorNode.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                BehaviorNode: the base of every behaviour-tree node, and
*                the three kinds - Action (no children), Modifier (one
*                child) and Composite (many children).
*
* \details
*                The same shape as Unity Behavior's nodes. A node runs in
*                three hooks:
*                  OnStart()                 - a run begins
*                  OnUpdate(float deltaTime) - every frame while it runs
*                  OnEnd()                   - the run finished or was cut short
*                and each frame it reports a BehaviorStatus: still Running,
*                or done with Success or Failure.
*
*                Built-in nodes are C++ classes (AI/Nodes/); designers'
*                own nodes will be C# scripts with the same three hooks,
*                once scripting can bind them.
*
*                Every enemy runs its OWN copy of a graph's nodes (Clone),
*                as in Unity, so a node can keep its progress - a timer, a
*                child index - in plain member variables.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <limits>       // Composite's unlimited MaxChildren
#include <memory>
#include <vector>

// Crucible engine
#include "ECS/Core/ECSCore.h"   // Entity
#include "Math/Math.h"          // vec2, for the editor position

namespace Crucible {

    class Blackboard;
    class World;
    struct NodeDescription;

    // How a node is doing this frame.
    enum class BehaviorStatus : std::uint8_t {

        Running,    // not finished: tick me again next frame
        Success,    // finished, and it worked
        Failure     // finished, and it didn't (or couldn't)

    };  // BEHAVIORSTATUS ENUM

    // What a node can reach while it runs: the world, the enemy it belongs to,
    // and that enemy's blackboard. One per enemy, owned by its graph instance.
    struct BehaviorContext {

        World*      world      = nullptr;
        Entity      self{};
        Blackboard* blackboard = nullptr;

    };  // BEHAVIORCONTEXT STRUCT

    class BehaviorNode {

    public:

        virtual ~BehaviorNode() = default;

        // --- Driving it (parents and the graph call these) ------------------ //

        /*!
            \brief
            Runs the node for one frame: starts it if it isn't running, updates
            it, and ends it once it reports Success or Failure.

            \details
            OnStart and the first OnUpdate happen in the SAME frame. Otherwise
            every node would cost a frame just to begin, and a Sequence of
            instant nodes (Log, Stop, Log...) would crawl one node per frame.

            \return
            This frame's status. After Success or Failure the node is idle, and
            the next Tick starts a fresh run.
        */
        BehaviorStatus Tick(float deltaTime);

        // Cuts a running node short: its running children first, then its own
        // OnEnd. Used when a parent stops early (a Sequence whose child failed,
        // a state the enemy left). Does nothing if it isn't running.
        void Abort();

        bool           IsRunning() const { return m_running; }
        BehaviorStatus LastStatus() const { return m_status; }

        // Points this node (and its children) at its enemy's context. Called
        // once, when the enemy's copy is made.
        virtual void Bind(BehaviorContext* context) { m_context = context; }

        // --- What it is ------------------------------------------------------ //

        // A deep copy: the node's settings and copies of all its children,
        // with no run in progress. How each enemy gets its own copy of a graph.
        virtual std::unique_ptr<BehaviorNode> Clone() const = 0;

        // Its name, category, description and id (NodeRegistry.h).
        virtual const NodeDescription& Description() const = 0;

        // How many children this kind of node can take: 0 (Action), 1
        // (Modifier) or no limit (Composite). The editor asks this to know when
        // a new connection needs Branch Options instead.
        virtual std::size_t MaxChildren() const { return 0; }

        // Children, for the graph, saving and the editor. None by default.
        virtual std::size_t   ChildCount() const { return 0; }
        virtual BehaviorNode* ChildAt(std::size_t) const { return nullptr; }

        // Adds a child at the end. False if this kind of node can't take one
        // (an Action never can; a Modifier only one).
        virtual bool AddChild(std::unique_ptr<BehaviorNode>) { return false; }

        // Takes `child` back out (the editor disconnecting or deleting it).
        // Null if it isn't one of this node's children.
        virtual std::unique_ptr<BehaviorNode> DetachChild(BehaviorNode*) { return nullptr; }

        // Puts the children in left-to-right order of their editor position -
        // the order they run in, as in Unity Behavior. Only Composites have an
        // order to change. The editor calls it after a node is moved or
        // connected, so where a node sits IS when it runs. (Virtual partly so
        // the Editor, outside Engine.dll, can call it without an export.)
        virtual void SortChildrenByEditorX() {}

        // Where the node sits in the graph editor. Editor-only: saved with the
        // graph so the layout survives, never read while the graph runs.
        Math::vec2 m_editorPosition{ 0.0f };

    protected:

        BehaviorNode() = default;

        // A copy keeps the settings but never a run in progress, or the
        // context of whoever it was copied from.
        BehaviorNode(const BehaviorNode& other)
            : m_editorPosition(other.m_editorPosition), m_context(nullptr), m_status(BehaviorStatus::Success), m_running(false) {}
        BehaviorNode& operator=(const BehaviorNode&) = delete;

        // --- The three hooks ------------------------------------------------- //

        // A run begins. Return Running to carry on into OnUpdate, or Success /
        // Failure to finish straight away (a Log has nothing left to do).
        virtual BehaviorStatus OnStart() { return BehaviorStatus::Running; }

        // Every frame while it runs.
        virtual BehaviorStatus OnUpdate(float deltaTime) = 0;

        // The run is over: it finished, or it was aborted. Undo anything that
        // must not outlive the run (Move To stops its agent here).
        virtual void OnEnd() {}

        // Kinds with children abort the running ones (Abort calls this first).
        virtual void AbortChildren() {}

        BehaviorContext& Context() const { return *m_context; }

    private:

        BehaviorContext* m_context = nullptr;
        BehaviorStatus   m_status  = BehaviorStatus::Success;
        bool             m_running = false;

    };  // BEHAVIORNODE CLASS

    // A leaf: does something itself (Wait, Move To, Log). No children.
    class Action : public BehaviorNode {

    protected:

        Action() = default;
        Action(const Action&) = default;

    };  // ACTION CLASS

    // Exactly one child, whose run it controls (On Start repeats it; later:
    // Inverter, Cooldown...).
    class Modifier : public BehaviorNode {

    public:

        void          Bind(BehaviorContext* context) override;
        std::size_t   MaxChildren() const override { return 1; }
        std::size_t   ChildCount() const override { return m_child ? 1u : 0u; }
        BehaviorNode* ChildAt(std::size_t index) const override { return index == 0 ? m_child.get() : nullptr; }
        bool          AddChild(std::unique_ptr<BehaviorNode> child) override;
        std::unique_ptr<BehaviorNode> DetachChild(BehaviorNode* child) override;

    protected:

        Modifier() = default;
        Modifier(const Modifier& other);   // copies the child too

        void AbortChildren() override { if (m_child) m_child->Abort(); }

        BehaviorNode* Child() const { return m_child.get(); }

    private:

        std::unique_ptr<BehaviorNode> m_child;

    };  // MODIFIER CLASS

    // Many children, run in an order it decides (Sequence, Try In Order...).
    class Composite : public BehaviorNode {

    public:

        void          Bind(BehaviorContext* context) override;
        std::size_t   MaxChildren() const override { return std::numeric_limits<std::size_t>::max(); }
        std::size_t   ChildCount() const override { return m_children.size(); }
        BehaviorNode* ChildAt(std::size_t index) const override { return index < m_children.size() ? m_children[index].get() : nullptr; }
        bool          AddChild(std::unique_ptr<BehaviorNode> child) override;
        std::unique_ptr<BehaviorNode> DetachChild(BehaviorNode* child) override;
        void                          SortChildrenByEditorX() override;

    protected:

        Composite() = default;
        Composite(const Composite& other);   // copies every child too

        void AbortChildren() override;

    private:

        std::vector<std::unique_ptr<BehaviorNode>> m_children;

    };  // COMPOSITE CLASS

}   // NAMESPACE CRUCIBLE

// Gives a node class its Clone() and Description(). Put it at the top of every
// concrete node class, after declaring `static const NodeDescription kDescription;`.
//
// Clone works by the class's own copy constructor: plain fields copy as they
// are, and Modifier's / Composite's copy constructors deep-copy the children,
// so a new node type never has to write any copying code itself.
#define CRUCIBLE_BEHAVIOR_NODE(Type)                                                                    \
    public:                                                                                             \
        std::unique_ptr<BehaviorNode> Clone() const override { return std::make_unique<Type>(*this); }  \
        const NodeDescription& Description() const override { return kDescription; }

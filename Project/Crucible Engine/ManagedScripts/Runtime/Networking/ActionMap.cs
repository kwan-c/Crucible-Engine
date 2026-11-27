/*************************************************************************
* \file:         ActionMap.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Game actions bound to keys and mouse buttons, read into
*                the 32 action bits Network.SetLocalInput sends.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    One script on every machine (the host and offline too) owns the map and
*    sends this machine's input each frame; gameplay reads it back with
*    Network.GetInput or PlayerActions.Of:
*
*        const int Fire = 0, Use = 1, Reload = 2;
*        ActionMap actions = new ActionMap()
*            .Bind(Fire, KeyCode.MouseLeft)
*            .Bind(Use, KeyCode.E)
*            .Bind(Reload, KeyCode.R);
*
*        public override void OnUpdate(float dt)
*        {
*            actions.Send(cameraPitch);   // players moved by the PlayerController
*            // or: actions.Send(move, yaw, pitch);
*        }
*
*    An action can have several keys (any of them holds it). A key that went
*    down and up inside one frame still counts as held for that frame, so
*    the press reaches WasPressed.
*
* \history
*    -v1.0       Bind, Unbind, Clear, Sample, Send
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible
{
    /// <summary>Game actions (0..31) bound to keys; Sample reads them, Send gives them to Network.</summary>
    public sealed class ActionMap
    {
        private struct Binding
        {
            public int Action;
            public KeyCode Key;
        }

        private readonly List<Binding> bindings = new List<Binding>();

        /// <summary>Binds <paramref name="key"/> to an action (0..31). Returns this map, to chain.</summary>
        public ActionMap Bind(int action, KeyCode key)
        {
            if (action < 0 || action >= NetInput.MaxActions)
            {
                Log.Error("ActionMap.Bind(" + action + ", " + key + "): actions are 0.." + (NetInput.MaxActions - 1));
                return this;
            }
            for (int i = 0; i < bindings.Count; ++i)
                if (bindings[i].Action == action && bindings[i].Key == key)
                    return this;
            bindings.Add(new Binding { Action = action, Key = key });
            return this;
        }

        /// <summary>Removes every key of an action.</summary>
        public void Unbind(int action)
        {
            bindings.RemoveAll(b => b.Action == action);
        }

        /// <summary>Removes every binding.</summary>
        public void Clear()
        {
            bindings.Clear();
        }

        /// <summary>The actions held now (bit i = action i), from this machine's keys.</summary>
        public uint Sample()
        {
            uint held = 0;
            for (int i = 0; i < bindings.Count; ++i)
            {
                Binding b = bindings[i];
                if (Input.IsDown(b.Key) || Input.IsPressed(b.Key))
                    held |= 1u << b.Action;
            }
            return held;
        }

        /// <summary>Samples and sends the actions and aim pitch (Network.SetLocalActions). Returns what was held.</summary>
        public uint Send(float pitchDegrees = 0f)
        {
            uint held = Sample();
            Network.SetLocalActions(held, pitchDegrees);
            return held;
        }

        /// <summary>Samples and sends the actions with a move, yaw and pitch (Network.SetLocalInput).</summary>
        public uint Send(Vector3 move, float yawDegrees, float pitchDegrees)
        {
            uint held = Sample();
            Network.SetLocalInput(move, yawDegrees, pitchDegrees, held);
            return held;
        }
    }
}

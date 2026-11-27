/*************************************************************************
* \file:         NetRouter.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         The networking helpers' central handler: every command and event
*                passes here once per machine, before any script hears it.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    The engine (EntityScriptSystem) calls Route once per command and event.
*    It runs the handlers registered with NetMessage.OnCommand / OnEvent,
*    and handles the helpers' own messages (kinds 0xFF00 to 0xFFFF: effects,
*    score requests), which no script ever hears.
*
*    Message kinds:
*        1 to 0x7FFF       numbered by hand (SendCommand(kind, ...))
*        0x8000 to 0xFEFF  named (NetMessage.Named)
*        0xFF00 to 0xFFFF  the helpers' own (reserved)
*
*    Not for game scripts: nothing here needs calling.
*
* \history
*    -v1.0       Route: named-message handlers, effects, score requests
*
**************************************************************************/
using System;

namespace Crucible
{
    /// <summary>The engine's entry for the networking helpers. Not for game scripts.</summary>
    public static class NetRouter
    {
        /// <summary>The helpers' own kinds start here; scripts never hear them.</summary>
        public const ushort FirstReservedKind = 0xFF00;

        internal const ushort EffectEvent = 0xFF01;     // host to all: play this effect
        internal const ushort EffectRequest = 0xFF02;   // client to host: I played this effect, tell the others
        internal const ushort ScoreRequest = 0xFF10;    // client to host: NetGameState.RequestScore

        private const int Command = 5, Event = 6;      // ScriptNetEvent::Kind (NetworkContext.h)

        /// <summary>
        /// Called by the engine once per command (kind 5) and event (kind 6). Returns 1 when the
        /// message was one of the helpers' own: the scripts do not hear it.
        /// </summary>
        public static int Route(int kind, int peer, int code, int serial, int targetIndex, int targetGeneration)
        {
            if (kind != Command && kind != Event)
                return 0;
            ushort messageKind = unchecked((ushort)code);
            Entity target = targetIndex >= 0 ? new Entity(unchecked((uint)targetIndex), unchecked((uint)targetGeneration)) : null;
            byte[] payload = Network.PayloadOf(serial);
            if (messageKind >= FirstReservedKind)
            {
                try
                {
                    if (kind == Event && messageKind == EffectEvent)
                        NetEffect.Receive(new NetReader(payload, payload.Length));
                    else if (kind == Command && messageKind == EffectRequest)
                        NetEffect.Relay(peer, payload);
                    else if (kind == Command && messageKind == ScoreRequest)
                        NetGameState.HandleScoreRequest(peer, new NetReader(payload, payload.Length));
                }
                catch (Exception e)
                {
                    Log.Error("Networking helpers: message " + messageKind.ToString("X4") + " failed: " + e.Message);
                }
                return 1;
            }
            NetMessage.Dispatch(kind == Command, peer, messageKind, payload, target);
            return 0;
        }
    }
}

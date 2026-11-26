/*************************************************************************
* \file:         PlayerActions.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         What a player does this frame (NetInput), and the input of
*                whoever owns an entity (PlayerActions.Of).
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Every machine gives its own player's input (Network.SetLocalInput, or
*    an ActionMap's Send); the host acts on everyone's. The same gameplay
*    code then runs offline, on the host, and for every client's player:
*
*        const int Fire = 0, Use = 1;
*
*        public override void OnUpdate(float dt)
*        {
*            if (!Network.IsAuthority) return;          // the host (or offline) decides
*            NetInput input = PlayerActions.Of(Entity);  // whoever owns this player
*            if (input.WasPressed(Fire)) Shoot(input.Yaw, input.Pitch);
*            if (input.IsDown(Use)) Interact();
*        }
*
*    WasPressed is true for exactly one frame per press, even for a tap
*    shorter than one network send. A player whose input stops arriving
*    (frozen, cut off) reads as neutral after about a quarter second:
*    nothing held, no move, facing and pitch kept.
*
*    C++ side: NetworkSession::SetLocalInput / GetPlayerInput.
*
* \history
*    -v1.0       NetInput, PlayerActions.Of
*
**************************************************************************/
namespace Crucible
{
    /// <summary>What one player does this frame (Network.GetInput, PlayerActions.Of).</summary>
    public struct NetInput
    {
        /// <summary>Actions are bits 0..31 of a uint.</summary>
        public const int MaxActions = 32;

        /// <summary>Neutral: nothing held, nothing pressed, no move.</summary>
        public static readonly NetInput None = new NetInput();

        /// <summary>Where the player wants to go: world X/Z, length up to 1 (Y is always 0).</summary>
        public Vector3 Move { get; private set; }
        /// <summary>Facing, degrees in [0, 360).</summary>
        public float Yaw { get; private set; }
        /// <summary>Aim pitch in degrees (-180, 180], as the player's script gave it.</summary>
        public float Pitch { get; private set; }
        /// <summary>Every action held: bit i = action i.</summary>
        public uint Held { get; private set; }
        /// <summary>Every action that went down since the previous frame: bit i = action i.</summary>
        public uint Pressed { get; private set; }
        /// <summary>Is input arriving? False before the first, and once it stopped (then neutral).</summary>
        public bool IsFresh { get; private set; }

        internal NetInput(Vector3 move, float yaw, float pitch, uint held, uint pressed, bool fresh) : this()
        {
            Move = move;
            Yaw = yaw;
            Pitch = pitch;
            Held = held;
            Pressed = pressed;
            IsFresh = fresh;
        }

        /// <summary>Is the action held this frame? Actions outside 0..31 never are.</summary>
        public bool IsDown(int action)
        {
            return action >= 0 && action < MaxActions && (Held & (1u << action)) != 0;
        }

        /// <summary>Did the action go down since the previous frame? True for one frame per press.</summary>
        public bool WasPressed(int action)
        {
            return action >= 0 && action < MaxActions && (Pressed & (1u << action)) != 0;
        }
    }

    /// <summary>The input of the player who owns an entity.</summary>
    public static class PlayerActions
    {
        /// <summary>
        /// The input of whoever owns <paramref name="entity"/> (Entity.Owner). The host reads any
        /// player's; a client only its own (other players' entities read as NetInput.None);
        /// offline, and for an entity that is not a network object, this machine's own.
        /// </summary>
        public static NetInput Of(Entity entity)
        {
            if (ReferenceEquals(entity, null) || !entity.IsAlive)
                return NetInput.None;
            return entity.IsMine ? Network.LocalInput : Network.GetInput(entity.Owner);
        }
    }
}

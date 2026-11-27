/*************************************************************************
* \file:         NetAnimDummy.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief         Networking sample: an animated character only the host
*                drives, to see replicated Animator parameters (Networking
*                C# Phase 7) on a client.
*
* \details
*    On the "Animation Dummy" in network_gamplay.scene: a network object
*    whose Network Object table replicates Transform/YawOnly and Animator,
*    with a "Body" child that has the golem model and the Enemy Fodder
*    controller. The host walks it back and forth, jogs it, stops it for a
*    melee attack (the Attack trigger) and a flinch (HitDirection + the Hit
*    trigger). It sets Float, Integer and Trigger parameters, the three
*    kinds the network sends.
*
*    Nothing here sends anything: the script only calls Animator as any
*    script would. The engine records the host's calls and every client's
*    copy of the dummy plays the same states. On a client this script does
*    nothing (its Animator calls would be refused: the host owns them).
*    Offline it runs as on a host.
*
* \history
*    -v1.0       Networking C# Phase 7 sample
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class NetAnimDummy : EntityScript
    {
        /// <summary>Metres per second while walking (the controller walks between 0.3 and 2.9).</summary>
        public float walkSpeed = 1.5f;
        /// <summary>Metres per second while jogging (above 2.9).</summary>
        public float jogSpeed = 3.5f;
        /// <summary>How far each leg of the walk goes, in metres along X.</summary>
        public float distance = 8f;
        /// <summary>Seconds it stands still after an attack or a flinch.</summary>
        public float pauseSeconds = 2f;

        // One loop: walk out, walk back, attack, jog out, jog back, flinch.
        private const int WalkOut = 0, WalkBack = 1, Attack = 2, JogOut = 3, JogBack = 4, Flinch = 5, Steps = 6;

        private Entity body;
        private Vector3 start;
        private int step;
        private float stepTime;
        private bool fired;     // this standing step's trigger was fired
        private bool warned;

        public override void OnCreate()
        {
            body = Entity.FindChild("Body");
            start = Transform.Position;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!Network.IsAuthority || body == null)
                return;

            stepTime += deltaTime;
            switch (step)
            {
                case WalkOut:  Move(+1f, walkSpeed, deltaTime); break;
                case WalkBack: Move(-1f, walkSpeed, deltaTime); break;
                case JogOut:   Move(+1f, jogSpeed, deltaTime); break;
                case JogBack:  Move(-1f, jogSpeed, deltaTime); break;
                case Attack:
                case Flinch:
                    Stand(step == Attack);
                    break;
            }
        }

        // Along X from `start` and back; the step ends at either end.
        private void Move(float direction, float speed, float deltaTime)
        {
            Set(body.Animator.SetFloat("Speed", speed));
            Vector3 position = Transform.Position;
            position.X += direction * speed * deltaTime;
            float travelled = position.X - start.X;
            bool done = direction > 0f ? travelled >= distance : travelled <= 0f;
            if (done)
                position.X = start.X + (direction > 0f ? distance : 0f);
            Transform.Position = position;
            Transform.Rotation = Quaternion.LookRotation(new Vector3(direction, 0f, 0f));
            if (done)
                Next();
        }

        // Stops; on the first frame fires the attack, or the flinch from the front.
        private void Stand(bool attack)
        {
            if (!fired)
            {
                fired = true;
                Set(body.Animator.SetFloat("Speed", 0f));
                if (attack)
                {
                    Set(body.Animator.SetTrigger("Attack"));
                }
                else
                {
                    Set(body.Animator.SetInteger("HitDirection", 0));
                    Set(body.Animator.SetTrigger("Hit"));
                }
            }
            if (stepTime >= pauseSeconds)
                Next();
        }

        private void Next()
        {
            step = (step + 1) % Steps;
            stepTime = 0f;
            fired = false;
        }

        private void Set(bool ok)
        {
            if (!ok && !warned)
            {
                warned = true;
                Log.Warning("NetAnimDummy: an Animator parameter could not be set - is the Body's controller Enemy Fodder?");
            }
        }
    }
}

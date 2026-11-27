/*************************************************************************
* \file:         LagTarget.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Networking sample: the lag demo's target, slid back and forth by the
*                host.
*
* \details
*    On "Lag Target" in network_sync_test.scene: the host moves it along X
*    between minX and maxX at `speed` metres a second; every other machine
*    sees it through its replicated Transform.
*
* \history
*    -v1.0       the moving target
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class LagTarget : EntityScript
    {
        public float minX = 4f;
        public float maxX = 12f;
        public float speed = 4f;

        private float direction = 1f;

        public override void OnUpdate(float deltaTime)
        {
            if (!Network.IsAuthority) return;
            Vector3 p = Entity.Transform.Position;
            p.X += direction * speed * deltaTime;
            if (p.X > maxX) { p.X = maxX; direction = -1f; }
            if (p.X < minX) { p.X = minX; direction = 1f; }
            Entity.Transform.Position = p;
        }
    }
}

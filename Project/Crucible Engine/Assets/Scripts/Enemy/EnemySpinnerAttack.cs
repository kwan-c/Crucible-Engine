using System;

namespace Crucible.Scripts
{
    public sealed partial class Enemy
    {
        public bool previewSpinnerWindup;
        public float spinnerWindup = 0.9f;
        public float spinnerRecovery = 0.8f;
        public float spinnerCooldown = 3.5f;

        float WindupSeconds { get { return previewSpinnerWindup ? Math.Max(0f, spinnerWindup) : fireDuration * releaseFrame; } }
        float AttackSeconds { get { return previewSpinnerWindup ? WindupSeconds + Math.Max(0f, spinnerRecovery) : fireDuration; } }

        // The existing attack clock owns the warning, cancellation and recovery.
        // Charge movement can be connected at release once the movement work is available.
        public bool TrySpinnerWindup()
        {
            return previewSpinnerWindup && RequestAttack();
        }
    }
}

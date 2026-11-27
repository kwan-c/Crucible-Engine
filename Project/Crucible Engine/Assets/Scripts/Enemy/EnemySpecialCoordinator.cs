namespace Crucible.Scripts
{
    // One scene host. Rates and caps apply within a neighbourhood against the same target.
    public sealed class EnemySpecialCoordinator : EntityScript
    {
        public float localRadius = 24f;
        public int maximumSpecialAttacks = 1;
        public float minimumStartSpacing = .55f;
        readonly EnemyAttackReservations pool = new EnemyAttackReservations(e => e.useSpecialCoordinator);
        static EnemySpecialCoordinator instance;
        public static EnemySpecialCoordinator Active { get { return instance != null && instance.Entity.IsActive
            && instance.Enabled && instance.Entity.IsEnabled<EntityScript>() ? instance : null; } }
        public override void OnCreate() { if (instance == null || !instance.Entity.IsAlive) instance = this; }
        public override void OnUpdate(float dt) { pool.Prune(Time.ElapsedTime); }
        public override void OnDestroy() { pool.Clear(); if (instance == this) instance = null; }
        internal bool TryAcquire(Enemy owner) { return pool.TryAcquire(owner, maximumSpecialAttacks, minimumStartSpacing, localRadius); }
        internal void Release(Enemy owner) { pool.Release(owner); }
    }

    public sealed partial class Enemy
    {
        EnemySpecialCoordinator specialHost;
        EnemyHordeCoordinator meleeHost;
        bool AdmitSpecialAttack()
        {
            specialHost = useSpecialCoordinator ? EnemySpecialCoordinator.Active : null;
            return specialHost == null || specialHost.TryAcquire(this);
        }
        void ReleaseSpecialAttack()
        {
            if (specialHost != null) { specialHost.Release(this); specialHost = null; }
        }
        bool AdmitMeleeAttack()
        {
            if (!melee || !useHordeCoordination) return true;
            meleeHost = EnemyHordeCoordinator.Active;
            return meleeHost == null || meleeHost.TryAdmit(this);
        }
        void ReleaseMeleeAttack()
        {
            if (meleeHost != null) { meleeHost.Release(this); meleeHost = null; }
        }
    }
}

/*************************************************************************
* \file:         ParticleEmitter.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief         Script access to an entity's ParticleEmitter component.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's ParticleSystem, the parts a script usually touches. Get it with
*    GetComponent<ParticleEmitter>() or Entity.ParticleEmitter:
*
*        var sparks = GetComponent<ParticleEmitter>();
*        if (hit) sparks.Emit(20);           // 20 sparks now, effect keeps going
*        if (reload) sparks.Restart();       // fire the whole run (and burst) again
*        sparks.Rate = speed * 4f;           // more exhaust when moving fast
*        if (!sparks.IsAlive) Destroy(Entity);   // finished effect: clean up
*
*    Particles fire around the entity's local +Y (not Forward): rotate the
*    entity to aim them. Everything applies the same frame (the particle
*    system runs after scripts).
*
*    Play starts a stopped emitter with a fresh run (its burst fires). Stop
*    stops new particles; live ones finish their lives. Restart starts a
*    fresh run even while playing. Emit spawns extra particles whether or not
*    it is playing. A non-looping emitter switches itself off when its
*    Duration is up (IsEmitting false; Play() runs it again), so IsAlive
*    turns false once its last particles fade. (Play mode only: in the editor
*    the preview leaves the Inspector's Playing ticked.)
*
*    Shape, texture, flipbook and blend mode are left to the Inspector.
*
*    C++ side: Engine/src/Scripting/Bindings/ParticleBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>An entity's particle effect. Unity's ParticleSystem.</summary>
    public sealed class ParticleEmitter
    {
        private readonly EntityIdentity entity;
        internal ParticleEmitter(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this emitter is on (rotate it to aim: particles go along local +Y).</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        // --- playing ------------------------------------------------------------ //

        /// <summary>Starts emitting. A stopped emitter begins a fresh run (its burst fires).</summary>
        public void Play() { ControlInternal(I, G, 0); }

        /// <summary>Stops new particles. Particles already alive finish their lives.</summary>
        public void Stop() { ControlInternal(I, G, 1); }

        /// <summary>
        /// Starts a fresh run this frame (the burst fires again), even if it is already
        /// playing - for re-firing a one-shot effect. Also turns it on if it was stopped.
        /// </summary>
        public void Restart() { ControlInternal(I, G, 2); }

        /// <summary>
        /// Spawns <paramref name="count"/> extra particles this frame, whether or not it is
        /// playing - e.g. sparks on each hit. They use the emitter's settings; MaxParticles
        /// still caps the total. Unity's ParticleSystem.Emit.
        /// </summary>
        public void Emit(int count) { EmitInternal(I, G, count); }

        /// <summary>
        /// Is it emitting? False after Stop, and once a non-looping run's Duration is up.
        /// (Particles may still be alive - see IsAlive.)
        /// </summary>
        public bool IsEmitting
        {
            get { int v; return GetPlayingInternal(I, G, out v) != 0 && v != 0; }
        }

        /// <summary>How many of its particles are alive (as of the last frame).</summary>
        public int ParticleCount
        {
            get { int v; return GetParticleCountInternal(I, G, out v) != 0 ? v : 0; }
        }

        /// <summary>
        /// Is it still doing anything - emitting, or with particles alive? False once a
        /// stopped or finished (non-looping) effect has faded out. Unity's IsAlive().
        /// </summary>
        public bool IsAlive
        {
            get { return IsEmitting || ParticleCount > 0; }
        }

        // --- spawning ------------------------------------------------------------ //

        /// <summary>Repeat the run forever, or stop spawning after Duration.</summary>
        public bool Loop
        {
            get { int v; return GetLoopInternal(I, G, out v) != 0 && v != 0; }
            set { SetLoopInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Length of one run, in seconds.</summary>
        public float Duration { get { float v; return GetDurationInternal(I, G, out v) != 0 ? v : 0f; } set { SetDurationInternal(I, G, ref value); } }

        /// <summary>Particles per second, continuously.</summary>
        public float Rate { get { float v; return GetRateInternal(I, G, out v) != 0 ? v : 0f; } set { SetRateInternal(I, G, ref value); } }

        /// <summary>Particles all at once at the start of each run.</summary>
        public int BurstCount
        {
            get { int v; return GetBurstCountInternal(I, G, out v) != 0 ? v : 0; }
            set { SetBurstCountInternal(I, G, value); }
        }

        /// <summary>Most particles alive at once.</summary>
        public int MaxParticles
        {
            get { int v; return GetMaxParticlesInternal(I, G, out v) != 0 ? v : 0; }
            set { SetMaxParticlesInternal(I, G, value); }
        }

        /// <summary>Half-angle of the cone of directions around local +Y, degrees (0 = straight, 180 = all round).</summary>
        public float Spread { get { float v; return GetSpreadInternal(I, G, out v) != 0 ? v : 0f; } set { SetSpreadInternal(I, G, ref value); } }

        /// <summary>True: particles stay where they were born when the emitter moves (a trail). False: they move with it.</summary>
        public bool WorldSpace
        {
            get { int v; return GetWorldSpaceInternal(I, G, out v) != 0 && v != 0; }
            set { SetWorldSpaceInternal(I, G, value ? 1 : 0); }
        }

        // --- each particle (picked at random between Min and Max) ---------------- //

        public float LifetimeMin { get { float v; return GetLifetimeMinInternal(I, G, out v) != 0 ? v : 0f; } set { SetLifetimeMinInternal(I, G, ref value); } }
        public float LifetimeMax { get { float v; return GetLifetimeMaxInternal(I, G, out v) != 0 ? v : 0f; } set { SetLifetimeMaxInternal(I, G, ref value); } }
        /// <summary>Starting speed, m/s.</summary>
        public float SpeedMin { get { float v; return GetSpeedMinInternal(I, G, out v) != 0 ? v : 0f; } set { SetSpeedMinInternal(I, G, ref value); } }
        public float SpeedMax { get { float v; return GetSpeedMaxInternal(I, G, out v) != 0 ? v : 0f; } set { SetSpeedMaxInternal(I, G, ref value); } }
        /// <summary>Starting width, metres.</summary>
        public float SizeMin { get { float v; return GetSizeMinInternal(I, G, out v) != 0 ? v : 0f; } set { SetSizeMinInternal(I, G, ref value); } }
        public float SizeMax { get { float v; return GetSizeMaxInternal(I, G, out v) != 0 ? v : 0f; } set { SetSizeMaxInternal(I, G, ref value); } }
        /// <summary>Size multiplier by the end of a particle's life (0.2 shrinks, 3 grows).</summary>
        public float EndSizeScale { get { float v; return GetEndSizeScaleInternal(I, G, out v) != 0 ? v : 0f; } set { SetEndSizeScaleInternal(I, G, ref value); } }
        /// <summary>1 falls like a rock, 0 floats, negative rises.</summary>
        public float GravityScale { get { float v; return GetGravityScaleInternal(I, G, out v) != 0 ? v : 0f; } set { SetGravityScaleInternal(I, G, ref value); } }
        /// <summary>How fast particles lose speed (per second).</summary>
        public float Drag { get { float v; return GetDragInternal(I, G, out v) != 0 ? v : 0f; } set { SetDragInternal(I, G, ref value); } }

        // --- look ---------------------------------------------------------------- //

        /// <summary>Colour and opacity (A) when a particle is born.</summary>
        public Color StartColor
        {
            get { Color c; return GetColorInternal(I, G, 0, out c) != 0 ? c : Color.White; }
            set { SetColorInternal(I, G, 0, ref value); }
        }

        /// <summary>Colour and opacity (A) when a particle dies; blended in between.</summary>
        public Color EndColor
        {
            get { Color c; return GetColorInternal(I, G, 1, out c) != 0 ? c : Color.White; }
            set { SetColorInternal(I, G, 1, ref value); }
        }

        /// <summary>Multiplies the colour. Above 1 it glows (bloom).</summary>
        public float Brightness { get { float v; return GetBrightnessInternal(I, G, out v) != 0 ? v : 0f; } set { SetBrightnessInternal(I, G, ref value); } }

        // --- light (one point light at the middle of the live particles) -------- //

        public bool EmitLight
        {
            get { int v; return GetEmitLightInternal(I, G, out v) != 0 && v != 0; }
            set { SetEmitLightInternal(I, G, value ? 1 : 0); }
        }

        public Color LightColor
        {
            get { Color c; return GetLightColorInternal(I, G, out c) != 0 ? c : Color.White; }
            set { SetLightColorInternal(I, G, ref value); }
        }

        public float LightIntensity { get { float v; return GetLightIntensityInternal(I, G, out v) != 0 ? v : 0f; } set { SetLightIntensityInternal(I, G, ref value); } }
        public float LightRange { get { float v; return GetLightRangeInternal(I, G, out v) != 0 ? v : 0f; } set { SetLightRangeInternal(I, G, ref value); } }

        public override string ToString() { return "ParticleEmitter on " + Entity; }

        // --- internal calls (ParticleBindings.cpp) ------------------------------ //


        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int ControlInternal(int index, int generation, int action);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int EmitInternal(int index, int generation, int count);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetParticleCountInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetPlayingInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLoopInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLoopInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetWorldSpaceInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetWorldSpaceInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetEmitLightInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetEmitLightInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBurstCountInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBurstCountInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetMaxParticlesInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetMaxParticlesInternal(int index, int generation, int value);

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetDurationInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetDurationInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetRateInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetRateInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSpreadInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSpreadInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLifetimeMinInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLifetimeMinInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLifetimeMaxInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLifetimeMaxInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSpeedMinInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSpeedMinInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSpeedMaxInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSpeedMaxInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSizeMinInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSizeMinInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSizeMaxInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSizeMaxInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetEndSizeScaleInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetEndSizeScaleInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetGravityScaleInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetGravityScaleInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetDragInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetDragInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBrightnessInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBrightnessInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLightIntensityInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLightIntensityInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLightRangeInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLightRangeInternal(int index, int generation, ref float value);

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetColorInternal(int index, int generation, int which, out Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetColorInternal(int index, int generation, int which, ref Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLightColorInternal(int index, int generation, out Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLightColorInternal(int index, int generation, ref Color value);
    }
}

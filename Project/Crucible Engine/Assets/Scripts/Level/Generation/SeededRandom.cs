/*************************************************************************
* \file:         SeededRandom.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief         A small random number generator whose sequence is fixed by
*                its seed, on every machine and every runtime.
*
* \details
*    Generation must give every player the same map from one seed, so it
*    never uses System.Random: its algorithm is the runtime's choice and is
*    not promised to stay the same. This is Mulberry32: one uint of state,
*    fast, and plenty random for level layout (not for cryptography).
*
*    Give each independent use its own generator, seeded from the map seed
*    mixed with a constant (seed ^ 0x...), so adding draws to one never
*    moves another, as the Unity generator did.
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class SeededRandom
    {
        private uint state;

        public SeededRandom(int seed) { state = unchecked((uint)seed); }

        /// <summary>The next 32 random bits.</summary>
        public uint NextUInt()
        {
            unchecked
            {
                state += 0x6D2B79F5u;
                uint t = state;
                t = (t ^ (t >> 15)) * (t | 1u);
                t ^= t + (t ^ (t >> 7)) * (t | 61u);
                return t ^ (t >> 14);
            }
        }

        /// <summary>0 (included) to 1 (excluded).</summary>
        public double NextDouble() { return NextUInt() / 4294967296.0; }

        /// <summary>0 (included) to 1 (excluded).</summary>
        public float NextFloat() { return (float)NextDouble(); }

        /// <summary>0 (included) to maxExclusive (excluded); 0 when maxExclusive is 0 or less.</summary>
        public int Next(int maxExclusive)
        {
            return maxExclusive <= 0 ? 0 : (int)(NextDouble() * maxExclusive);
        }

        /// <summary>min (included) to maxExclusive (excluded); min when the range is empty.</summary>
        public int Next(int min, int maxExclusive)
        {
            return maxExclusive <= min ? min : min + (int)(NextDouble() * ((long)maxExclusive - min));
        }

        /// <summary>min to max, both included.</summary>
        public float Range(float min, float max) { return min + (max - min) * NextFloat(); }
    }
}

/*************************************************************************
* \file:         SeededNoise.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief         2D gradient (Perlin) noise from a seed: the same seed
*                gives the same noise everywhere.
*
* \details
*    Ported from the Unity prototype's SeededNoise (Level/Generation).
*    Changes: the permutation shuffle draws from SeededRandom instead of
*    System.Random, and the tuple swap is written out (C# 7.2). Because the
*    shuffle is different, a seed gives a different map than it did in
*    Unity; the shapes and settings behave the same.
*
*    Sample returns roughly -1..1; Layered adds octaves and returns 0..1.
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class SeededNoise
    {
        private readonly int[] permutation = new int[512];

        public SeededNoise(int seed)
        {
            SeededRandom random = new SeededRandom(seed);
            int[] p = new int[256];
            for (int i = 0; i < 256; i++) p[i] = i;
            for (int i = 255; i > 0; i--)
            {
                int j = random.Next(i + 1);
                int swap = p[i]; p[i] = p[j]; p[j] = swap;
            }
            for (int i = 0; i < 512; i++) permutation[i] = p[i & 255];
        }

        public float Sample(float x, float y)
        {
            int xi = (int)System.Math.Floor(x) & 255, yi = (int)System.Math.Floor(y) & 255;
            float xf = x - (float)System.Math.Floor(x), yf = y - (float)System.Math.Floor(y);
            float u = Fade(xf), v = Fade(yf);
            int aa = permutation[permutation[xi] + yi], ab = permutation[permutation[xi] + yi + 1];
            int ba = permutation[permutation[xi + 1] + yi], bb = permutation[permutation[xi + 1] + yi + 1];
            float x1 = Lerp(Gradient(aa, xf, yf), Gradient(ba, xf - 1f, yf), u);
            float x2 = Lerp(Gradient(ab, xf, yf - 1f), Gradient(bb, xf - 1f, yf - 1f), u);
            return Lerp(x1, x2, v);
        }

        // Several layers added together: large hills plus smaller bumps. Returns 0..1.
        public float Layered(float x, float y, int layers, float persistence, float lacunarity)
        {
            float total = 0f, amplitude = 1f, frequency = 1f, max = 0f;
            for (int i = 0; i < System.Math.Max(1, layers); i++)
            {
                total += Sample(x * frequency, y * frequency) * amplitude;
                max += amplitude;
                amplitude *= persistence;
                frequency *= lacunarity;
            }
            float normalised = (total / max + 1f) * 0.5f;
            return System.Math.Max(0f, System.Math.Min(1f, normalised));
        }

        private static float Fade(float t) { return t * t * t * (t * (t * 6f - 15f) + 10f); }
        private static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        private static float Gradient(int hash, float x, float y)
        {
            switch (hash & 7)
            {
                case 0: return x + y;
                case 1: return -x + y;
                case 2: return x - y;
                case 3: return -x - y;
                case 4: return x;
                case 5: return -x;
                case 6: return y;
                default: return -y;
            }
        }
    }
}

/*************************************************************************
* \file:         SeededNoise.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.1
* \brief         2D and 3D gradient (Perlin) noise from a seed: the same
*                seed gives the same noise everywhere.
*
* \details
*    Ported from the Unity prototype's SeededNoise (Level/Generation).
*    Changes: the permutation shuffle draws from SeededRandom instead of
*    System.Random, and the tuple swap is written out (C# 7.2). Because the
*    shuffle is different, a seed gives a different map than it did in
*    Unity; the shapes and settings behave the same.
*
*    Sample returns roughly -1..1; Layered adds octaves and returns 0..1.
*    Sample3 / Layered3 are the same in 3D (Ken Perlin's improved noise,
*    2002), from the same permutation; the 2D ones are unchanged.
*
* \history
*    -v1.0       ported from Unity
*    -v1.1       Sample3 / Layered3 (3D, for cave walls)
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

        // 3D noise at (x, y, z), roughly -1..1.
        public float Sample3(float x, float y, float z)
        {
            float fx = (float)System.Math.Floor(x), fy = (float)System.Math.Floor(y), fz = (float)System.Math.Floor(z);
            int xi = (int)fx & 255, yi = (int)fy & 255, zi = (int)fz & 255;
            x -= fx; y -= fy; z -= fz;
            float u = Fade(x), v = Fade(y), w = Fade(z);
            int a = permutation[xi] + yi, aa = permutation[a] + zi, ab = permutation[a + 1] + zi;
            int b = permutation[xi + 1] + yi, ba = permutation[b] + zi, bb = permutation[b + 1] + zi;
            return Lerp(Lerp(Lerp(Gradient3(permutation[aa], x, y, z), Gradient3(permutation[ba], x - 1f, y, z), u),
                             Lerp(Gradient3(permutation[ab], x, y - 1f, z), Gradient3(permutation[bb], x - 1f, y - 1f, z), u), v),
                        Lerp(Lerp(Gradient3(permutation[aa + 1], x, y, z - 1f), Gradient3(permutation[ba + 1], x - 1f, y, z - 1f), u),
                             Lerp(Gradient3(permutation[ab + 1], x, y - 1f, z - 1f), Gradient3(permutation[bb + 1], x - 1f, y - 1f, z - 1f), u), v), w);
        }

        // Several 3D layers added together, like Layered. Returns 0..1.
        public float Layered3(float x, float y, float z, int layers, float persistence, float lacunarity)
        {
            float total = 0f, amplitude = 1f, frequency = 1f, max = 0f;
            for (int i = 0; i < System.Math.Max(1, layers); i++)
            {
                total += Sample3(x * frequency, y * frequency, z * frequency) * amplitude;
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

        // Perlin's 12 cube-edge directions (with 4 repeated to fill 16).
        private static float Gradient3(int hash, float x, float y, float z)
        {
            int h = hash & 15;
            float u = h < 8 ? x : y;
            float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
        }
    }
}

using System;

namespace Crucible.Examples
{
    // Standalone managed logic. No entity, physics, Animator or other engine APIs.
    public sealed class Counter
    {
        private int value;

        public int Add(int amount)
        {
            if (amount < 0) throw new ArgumentOutOfRangeException("amount", "Use a nonnegative amount.");
            value += amount;
            return value;
        }

        public void Reset() { value = 0; }
        public string Describe() { return "Counter = " + value; }
        public string Run() { Add(7); return "C# DLL executed: " + Describe(); }
        public static int Multiply(int left, int right) { return left * right; }
        public static string Echo(string text) { return text; }
    }
}

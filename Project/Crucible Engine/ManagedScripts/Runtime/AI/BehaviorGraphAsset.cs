namespace Crucible.AI
{
    /// <summary>
    /// A node field that names a .behaviorgraph file (by asset id). The graph window shows it as
    /// a picker you can also drop a .behaviorgraph onto from the Asset Browser.
    /// </summary>
    public sealed class BehaviorGraphAsset
    {
        /// <summary>The asset id of the .behaviorgraph, or 0 for none.</summary>
        public ulong Id;

        public override string ToString() { return Id == 0 ? "none" : Id.ToString(); }
    }
}

using System;
using System.Runtime.CompilerServices;

namespace Crucible.AI
{
    /// <summary>A node's or condition's name, description, menu category and file id (Unity's NodeDescription).</summary>
    [AttributeUsage(AttributeTargets.Class, Inherited = false, AllowMultiple = false)]
    public sealed class NodeDescriptionAttribute : Attribute
    {
        public string Name { get; private set; }
        public string Description { get; private set; }
        public string Category { get; private set; }
        public string Id { get; private set; }
        public string File { get; private set; }

        public NodeDescriptionAttribute(string name, string description = "", string category = "", string id = "",
                                        [CallerFilePath] string file = "")
        {
            Name = name ?? "";
            Description = description ?? "";
            Category = category ?? "";
            Id = id ?? "";
            File = file ?? "";
        }
    }
}

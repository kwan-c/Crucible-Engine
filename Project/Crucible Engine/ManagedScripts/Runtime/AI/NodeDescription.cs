using System;
using System.Runtime.CompilerServices;

namespace Crucible.AI
{
    /// <summary>A node's or condition's name, description, story, menu category and file id (Unity's NodeDescription).</summary>
    /// <remarks>
    /// Written the Unity way, with names, so an attribute copied from Unity Behavior works as it is:
    /// <code>
    /// [NodeDescription(
    ///     name: "Navigate To Location",
    ///     description: "Navigates the agent to a position.",
    ///     story: "[Agent] navigates to [Location]",
    ///     category: "Action/Navigation",
    ///     id: "c67c5c55de9fe94897cf61976250cc83")]
    /// </code>
    /// Every built-in and template is written this way. In order also works: (name, description,
    /// category, id) - note that's not Unity's order, so prefer the names.
    /// story is the sentence drawn on the node; a [Word] in it shows the field of that name.
    /// A '/' in category makes a submenu in Add Node. id is what graph files store: once a graph
    /// uses the node, never change it (left out, the class name is used). icon is accepted so
    /// Unity attributes compile, but not shown yet. file is filled in by the compiler
    /// ([CallerFilePath]) - it's how Inspect Script finds the .cs - don't pass it.
    /// </remarks>
    [AttributeUsage(AttributeTargets.Class, Inherited = false, AllowMultiple = false)]
    public sealed class NodeDescriptionAttribute : Attribute
    {
        public string Name { get; private set; }
        public string Description { get; private set; }
        public string Category { get; private set; }
        public string Id { get; private set; }
        public string Story { get; private set; }
        public string Icon { get; private set; }
        public string File { get; private set; }

        // story and icon come after id, so a script written in order - (name, description,
        // category, id) - keeps its meaning. Named values can come in any order.
        public NodeDescriptionAttribute(string name = "", string description = "", string category = "", string id = "",
                                        string story = "", string icon = "", [CallerFilePath] string file = "")
        {
            Name = name ?? "";
            Description = description ?? "";
            Category = category ?? "";
            Id = id ?? "";
            Story = story ?? "";
            Icon = icon ?? "";
            File = file ?? "";
        }
    }
}

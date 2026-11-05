using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text;

namespace Crucible.AI
{
    /// <summary>For the engine only: builds and runs the C# behaviour trees by id.</summary>
    /// <remarks>
    /// The engine (C++) can only pass numbers and strings, so every node and condition built
    /// here gets an int id. C++ (AI/NodeHostBridge.cpp) calls Create, SetField / LinkField and
    /// AddChild once per enemy, then Tick(root) once per frame - composites run their children
    /// in here. Field values travel as text: invariant numbers, "true"/"false", Vector3 "x,y,z",
    /// Entity "index:generation", enum names; a file key is the field name with a lower-case
    /// first letter. A node that throws stops its tree until the next Play (logged once).
    /// </remarks>
    public static class NodeHost
    {
        private static readonly Dictionary<int, object> objects = new Dictionary<int, object>();
        private static readonly Dictionary<object, int> idOf = new Dictionary<object, int>();   // reference keys: nodes don't override Equals
        // Sets kept as Dictionary<..., bool>, NOT HashSet: HashSet lives in System.Core.dll,
        // which the engine's Mono can't load (it has no GAC to find framework DLLs in), so
        // NodeHost itself would fail to load. Dictionary is in mscorlib, which always loads.
        private static readonly Dictionary<int, bool> faulted = new Dictionary<int, bool>();      // trees and conditions that threw
        private static readonly Dictionary<string, bool> missing = new Dictionary<string, bool>(); // unknown ids already warned about
        private static int nextId = 1;
        private static Dictionary<string, Type> types;                                          // file id -> class

        // ================================================================== //
        // Listing the types (the editor)
        // ================================================================== //

        /// <summary>
        /// Every node and condition class with a [NodeDescription], in every loaded script DLL,
        /// as JSON: { "nodes": [ { id, class, name, category, description, kind, file, fields: [...] } ] }.
        /// Rescans each time, so newly compiled scripts appear.
        /// </summary>
        public static string Catalog()
        {
            types = null;
            var json = new StringBuilder("{\"nodes\":[");
            bool first = true;
            foreach (KeyValuePair<string, Type> pair in Types())
            {
                if (!first) json.Append(',');
                first = false;
                AppendEntry(json, pair.Key, pair.Value);
            }
            json.Append("]}");
            return json.ToString();
        }

        // ================================================================== //
        // Building a tree
        // ================================================================== //

        /// <summary>A new node or condition of type <paramref name="typeId"/> for one enemy. 0 if unknown or it threw.</summary>
        public static int Create(string typeId, int selfIndex, int selfGeneration)
        {
            Type type;
            if (typeId == null || (!Types().TryGetValue(typeId, out type) && !Rescan(typeId, out type)))
            {
                // Once per id: with a horde, the same missing node is asked for by every enemy.
                string key = typeId ?? "";
                if (!missing.ContainsKey(key))
                {
                    missing[key] = true;
                    Log.Warning("[AI] No C# node or condition with the id \"" + typeId + "\" - was its script renamed or deleted? It's left out.");
                }
                return 0;
            }

            object made;
            try { made = Activator.CreateInstance(type); }
            catch (Exception e) { Log.Error("[AI] Could not create " + type.Name + ": " + (e.InnerException ?? e)); return 0; }   // the constructor's own exception, if it threw

            Entity self = selfIndex != 0 ? new Entity(unchecked((uint)selfIndex), unchecked((uint)selfGeneration)) : null;
            var node = made as Node;
            var condition = made as Condition;
            if (node != null) node.Self = self;
            if (condition != null) condition.Self = self;

            // Every BlackboardVariable field gets an object (in case the script left
            // one null) and learns whose variables a link reads.
            foreach (FieldInfo field in made.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!typeof(BlackboardVariable).IsAssignableFrom(field.FieldType)) continue;
                var variable = field.GetValue(made) as BlackboardVariable;
                if (variable == null)
                {
                    variable = (BlackboardVariable)Activator.CreateInstance(field.FieldType);
                    field.SetValue(made, variable);
                }
                variable.Owner = self;
            }

            int id = nextId++;
            objects[id] = made;
            idOf[made] = id;
            return id;
        }

        /// <summary>Sets a field to a value given as text (a BlackboardVariable stops being linked). 1 = done.</summary>
        public static int SetField(int id, string field, string text)
        {
            object target;
            FieldInfo info;
            if (!Find(id, field, out target, out info)) return 0;

            var variable = info.GetValue(target) as BlackboardVariable;
            object value;
            if (!TryParse(variable != null ? variable.ValueType : info.FieldType, text ?? "", out value))
            {
                Log.Warning("[AI] " + target.GetType().Name + "." + info.Name + ": can't read \"" + text + "\" as " + TypeName(info.FieldType));
                return 0;
            }
            if (variable != null) { variable.BoxedValue = value; variable.LinkedVariable = null; }
            else info.SetValue(target, value);
            return 1;
        }

        /// <summary>Links a BlackboardVariable field to the graph variable <paramref name="variable"/>. 1 = done.</summary>
        public static int LinkField(int id, string field, string variable)
        {
            object target;
            FieldInfo info;
            if (!Find(id, field, out target, out info)) return 0;
            var linkable = info.GetValue(target) as BlackboardVariable;
            if (linkable == null) return 0;   // a plain field can't be linked
            linkable.LinkedVariable = variable;
            return 1;
        }

        /// <summary>Puts <paramref name="childId"/> under <paramref name="parentId"/>, last. 1 = done.</summary>
        public static int AddChild(int parentId, int childId)
        {
            object parent, child;
            if (!objects.TryGetValue(parentId, out parent) || !objects.TryGetValue(childId, out child)) return 0;
            var parentNode = parent as Node;
            var childNode = child as Node;
            return parentNode != null && childNode != null && parentNode.AddChild(childNode) ? 1 : 0;
        }

        // ================================================================== //
        // Running
        // ================================================================== //

        /// <summary>One frame of the tree under <paramref name="rootId"/>. Returns a Node.Status number.</summary>
        public static int Tick(int rootId, float deltaTime)
        {
            object found;
            var root = objects.TryGetValue(rootId, out found) ? found as Node : null;
            if (root == null || faulted.ContainsKey(rootId)) return (int)Node.Status.Failure;
            try
            {
                return (int)root.Tick(deltaTime);
            }
            catch (Exception e)
            {
                faulted[rootId] = true;
                Log.Error("[AI] A node on " + root.Self + " threw, so its tree stops until the next Play: " + e);
                return (int)Node.Status.Failure;
            }
        }

        /// <summary>Cuts the tree under <paramref name="rootId"/> short: every running node gets OnEnd.</summary>
        public static void Abort(int rootId)
        {
            object found;
            var root = objects.TryGetValue(rootId, out found) ? found as Node : null;
            if (root == null || faulted.ContainsKey(rootId)) return;
            try { root.Abort(); }
            catch (Exception e) { faulted[rootId] = true; Log.Error("[AI] A node on " + root.Self + " threw while stopping: " + e); }
        }

        /// <summary>
        /// One arrow condition. timeInState, finished (0/1) and finishedWith (a Node.Status
        /// number) describe the enemy's current state. 1 = true.
        /// </summary>
        public static int IsTrue(int id, float timeInState, int finished, int finishedWith)
        {
            object found;
            var condition = objects.TryGetValue(id, out found) ? found as Condition : null;
            if (condition == null || faulted.ContainsKey(id)) return 0;
            condition.TimeInState = timeInState;
            condition.StateFinished = finished != 0;
            condition.StateFinishedWith = (Node.Status)finishedWith;
            try
            {
                return condition.IsTrue() ? 1 : 0;
            }
            catch (Exception e)
            {
                faulted[id] = true;
                Log.Error("[AI] Condition" + condition.GetType().Name + " on " + condition.Self + " threw, so it stays false until the next Play: " + e);
                return 0;
            }
        }

        // ================================================================== //
        // Forgetting
        // ================================================================== //

        /// <summary>Forgets a condition, or a tree and every node under it.</summary>
        public static void Release(int id)
        {
            object found;
            if (!objects.TryGetValue(id, out found)) return;
            var pending = new List<object> { found };
            var children = new List<Node>();
            while (pending.Count > 0)
            {
                object next = pending[pending.Count - 1];
                pending.RemoveAt(pending.Count - 1);
                int nextId;
                if (idOf.TryGetValue(next, out nextId)) { objects.Remove(nextId); faulted.Remove(nextId); idOf.Remove(next); }
                var node = next as Node;
                if (node == null) continue;
                children.Clear();
                node.CollectChildren(children);
                foreach (Node child in children) pending.Add(child);
            }
        }

        /// <summary>How many nodes and conditions are alive: should drop to 0 after Stop.</summary>
        public static int LiveCount() { return objects.Count; }

        // ================================================================== //
        // Helpers
        // ================================================================== //

        // Every concrete Node / Condition class with a [NodeDescription] and a
        // public no-argument constructor, by file id. Only DLLs that can see
        // this one are searched (not the whole .NET library).
        private static Dictionary<string, Type> Types()
        {
            if (types != null) return types;
            types = new Dictionary<string, Type>(StringComparer.Ordinal);
            Assembly core = typeof(Node).Assembly;
            string coreName = core.GetName().Name;
            foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                if (assembly != core && Array.TrueForAll(assembly.GetReferencedAssemblies(), r => r.Name != coreName)) continue;
                Type[] all;
                try { all = assembly.GetTypes(); }
                catch (ReflectionTypeLoadException e) { all = e.Types; }
                foreach (Type type in all)
                {
                    if (type == null || !type.IsClass || type.IsAbstract) continue;
                    if (!typeof(Node).IsAssignableFrom(type) && !typeof(Condition).IsAssignableFrom(type)) continue;
                    var description = (NodeDescriptionAttribute)Attribute.GetCustomAttribute(type, typeof(NodeDescriptionAttribute));
                    if (description == null || type.GetConstructor(Type.EmptyTypes) == null) continue;
                    string id = description.Id.Length > 0 ? description.Id : type.Name;
                    if (types.ContainsKey(id))
                    {
                        Log.Warning("[AI] Two nodes use the id \"" + id + "\": " + types[id].FullName + " and " + type.FullName + ". The second is ignored.");
                        continue;
                    }
                    types.Add(id, type);
                }
            }
            return types;
        }

        // An id not found: maybe its DLL loaded after the last scan.
        private static bool Rescan(string typeId, out Type type)
        {
            types = null;
            return Types().TryGetValue(typeId, out type);
        }

        private static bool Find(int id, string field, out object target, out FieldInfo info)
        {
            info = null;
            if (!objects.TryGetValue(id, out target) || string.IsNullOrEmpty(field)) return false;
            foreach (FieldInfo candidate in EditableFields(target.GetType()))
            {
                if (!string.Equals(candidate.Name, field, StringComparison.OrdinalIgnoreCase)) continue;
                info = candidate;
                return true;
            }
            Log.Warning("[AI] " + target.GetType().Name + " has no field \"" + field + "\" - renamed in its script? The graph's value is ignored.");
            return false;
        }

        // Public instance fields of a type the editor can show, in declaration order.
        private static IEnumerable<FieldInfo> EditableFields(Type type)
        {
            foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
                if (TypeName(field.FieldType) != null) yield return field;
        }

        // The editor's name for a field type, or null if it isn't one it can show.
        private static string TypeName(Type type)
        {
            if (typeof(BlackboardVariable).IsAssignableFrom(type))
            {
                if (!type.IsGenericType) return null;
                Type inner = type.GetGenericArguments()[0];
                return inner == typeof(string) || inner.IsEnum ? null : TypeName(inner);   // the graph's variables have no strings or enums
            }
            if (type == typeof(float)) return "float";
            if (type == typeof(int)) return "int";
            if (type == typeof(bool)) return "bool";
            if (type == typeof(string)) return "string";
            if (type == typeof(Vector3)) return "Vector3";
            if (type == typeof(Entity)) return "Entity";
            if (type == typeof(BehaviorGraphAsset)) return "BehaviorGraph";
            if (type.IsEnum) return "enum";
            return null;
        }

        private static bool TryParse(Type type, string text, out object value)
        {
            value = null;
            CultureInfo invariant = CultureInfo.InvariantCulture;
            if (type == typeof(string)) { value = text; return true; }
            if (type == typeof(float)) { float f; if (!float.TryParse(text, NumberStyles.Float, invariant, out f)) return false; value = f; return true; }
            if (type == typeof(int)) { int i; if (!int.TryParse(text, NumberStyles.Integer, invariant, out i)) return false; value = i; return true; }
            if (type == typeof(bool)) { bool b; if (!bool.TryParse(text, out b)) return false; value = b; return true; }
            if (type == typeof(Vector3))
            {
                string[] parts = text.Split(',');
                float x, y, z;
                if (parts.Length != 3 || !float.TryParse(parts[0], NumberStyles.Float, invariant, out x)
                    || !float.TryParse(parts[1], NumberStyles.Float, invariant, out y) || !float.TryParse(parts[2], NumberStyles.Float, invariant, out z))
                    return false;
                value = new Vector3(x, y, z);
                return true;
            }
            if (type == typeof(BehaviorGraphAsset))
            {
                ulong id = 0;   // "" = none chosen
                if (text.Length > 0 && !ulong.TryParse(text, NumberStyles.Integer, invariant, out id)) return false;
                value = new BehaviorGraphAsset { Id = id };
                return true;
            }
            if (type == typeof(Entity))
            {
                if (text.Length == 0) { value = null; return true; }
                string[] parts = text.Split(':');
                uint index, generation;
                if (parts.Length != 2 || !uint.TryParse(parts[0], out index) || !uint.TryParse(parts[1], out generation)) return false;
                value = index != 0 ? new Entity(index, generation) : null;
                return true;
            }
            if (type.IsEnum)
            {
                try { value = Enum.Parse(type, text, true); return true; }
                catch (ArgumentException) { return false; }
            }
            return false;
        }

        private static string Format(object value)
        {
            CultureInfo invariant = CultureInfo.InvariantCulture;
            if (value == null) return "";
            if (value is float) return ((float)value).ToString("R", invariant);
            if (value is int) return ((int)value).ToString(invariant);
            if (value is bool) return (bool)value ? "true" : "false";
            if (value is Vector3)
            {
                var v = (Vector3)value;
                return v.X.ToString("R", invariant) + "," + v.Y.ToString("R", invariant) + "," + v.Z.ToString("R", invariant);
            }
            var graph = value as BehaviorGraphAsset;
            if (graph != null) return graph.Id == 0 ? "" : graph.Id.ToString(invariant);
            var entity = value as Entity;
            if (!ReferenceEquals(entity, null)) return entity.Index + ":" + entity.Generation;
            return value.ToString();   // string, enum name
        }

        private static void AppendEntry(StringBuilder json, string id, Type type)
        {
            var description = (NodeDescriptionAttribute)Attribute.GetCustomAttribute(type, typeof(NodeDescriptionAttribute));
            string kind = typeof(Condition).IsAssignableFrom(type) ? "condition"
                        : typeof(Composite).IsAssignableFrom(type) ? "composite"
                        : typeof(Modifier).IsAssignableFrom(type) ? "modifier" : "action";

            json.Append("{\"id\":").Append(Quote(id))
                .Append(",\"class\":").Append(Quote(type.FullName))
                .Append(",\"name\":").Append(Quote(description.Name))
                .Append(",\"category\":").Append(Quote(description.Category))
                .Append(",\"description\":").Append(Quote(description.Description))
                .Append(",\"kind\":").Append(Quote(kind))
                .Append(",\"file\":").Append(Quote(description.File))
                .Append(",\"fields\":[");

            // Defaults come from a fresh object: whatever the script's initialisers say.
            object sample = null;
            try { sample = Activator.CreateInstance(type); }
            catch (Exception) {}

            bool first = true;
            foreach (FieldInfo field in EditableFields(type))
            {
                if (!first) json.Append(',');
                first = false;

                object current = sample != null ? field.GetValue(sample) : null;
                var variable = current as BlackboardVariable;
                Type valueType = variable != null ? variable.ValueType
                               : typeof(BlackboardVariable).IsAssignableFrom(field.FieldType) ? field.FieldType.GetGenericArguments()[0] : field.FieldType;
                object value = variable != null ? variable.BoxedValue : current;
                if (value == null && valueType.IsValueType) value = Activator.CreateInstance(valueType);

                string key = char.ToLowerInvariant(field.Name[0]) + field.Name.Substring(1);
                json.Append("{\"name\":").Append(Quote(key))
                    .Append(",\"label\":").Append(Quote(field.Name))
                    .Append(",\"type\":").Append(Quote(TypeName(field.FieldType)))
                    .Append(",\"linkable\":").Append(typeof(BlackboardVariable).IsAssignableFrom(field.FieldType) ? "true" : "false")
                    .Append(",\"value\":").Append(Quote(Format(value)))
                    .Append(",\"link\":").Append(Quote(variable != null && variable.IsLinked ? variable.LinkedVariable : ""));
                if (valueType.IsEnum)
                {
                    json.Append(",\"options\":[");
                    string[] names = Enum.GetNames(valueType);
                    for (int i = 0; i < names.Length; ++i) json.Append(i > 0 ? "," : "").Append(Quote(names[i]));
                    json.Append(']');
                }
                json.Append('}');
            }
            json.Append("]}");
        }

        private static string Quote(string text)
        {
            var quoted = new StringBuilder("\"");
            foreach (char c in text ?? "")
            {
                switch (c)
                {
                    case '"': quoted.Append("\\\""); break;
                    case '\\': quoted.Append("\\\\"); break;
                    case '\n': quoted.Append("\\n"); break;
                    case '\r': quoted.Append("\\r"); break;
                    case '\t': quoted.Append("\\t"); break;
                    default:
                        if (c < ' ') quoted.Append("\\u").Append(((int)c).ToString("x4"));
                        else quoted.Append(c);
                        break;
                }
            }
            return quoted.Append('"').ToString();
        }
    }
}

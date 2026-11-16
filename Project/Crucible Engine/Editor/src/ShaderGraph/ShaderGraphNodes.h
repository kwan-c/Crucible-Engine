/*!
* @file			ShaderGraphNodes.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		The node table: every kind of node a shader graph can have, one row each.
*
* A row says what the node is called, which menu it is under, its input pins (type, and what
* an unconnected pin is worth), its output pins, and for each output the Slang expression that
* computes it. ShaderGraphGenerator fills the expressions in; nothing else in the code knows
* any node by name (apart from the two special rows, Output and Parameter).
*
* ADDING A NODE is adding a row to the table in ShaderGraphNodes.cpp. See the top of that
* file for how a row reads.
*
* Pin types: float, float2, float3, float4, texture - and "any" for a pin of a node that works
* on whatever is plugged in (Add, Lerp, Split ...). Such a node works in ONE type: the widest
* of what its "any" inputs are given. The rules for a link:
*   - a float goes into any numeric pin (it is repeated: 0.5 -> (0.5, 0.5, 0.5));
*   - otherwise the types must be the same - a vector gets narrower only through a Split node
*     and wider only through a Vector node;
*   - a texture goes only into a texture pin, and only a texture goes into one.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include <string>

namespace Crucible::ShaderGraph {

	/// What a pin carries. Any: decided per node by what is plugged in (see the file comment).
	enum class ValueType : uint8_t { Float, Float2, Float3, Float4, Texture, Any };

	/// "float", "float2", "float3", "float4", "texture", "any" - the Slang type's name for the first four.
	const char* ValueTypeName(ValueType type);

	/// 1 to 4 for float to float4; 0 for texture and any.
	uint32_t ComponentCount(ValueType type);

	/// The numeric type with this many components (1 to 4).
	ValueType NumericType(uint32_t components);

	struct InputPin {
		const char*          name;
		ValueType            type;
		/// What the pin is worth with nothing plugged in and no constant set on the node. An
		/// "any" pin uses only the first number (a float, which fits whatever the node works in).
		std::array<float, 4> defaultValue{};
		/// If set, an unconnected pin is this Slang expression instead of a constant - "s.uv"
		/// for a UV pin - and the node has no constant to edit for it.
		const char*          defaultCode = nullptr;
		bool                 colour = false;   ///< a float3 that is a colour: edited with a colour picker
	};

	struct OutputPin {
		const char* name;
		ValueType   type;    ///< Any = the type the node works in
		/// The Slang expression for this pin. {name} stands for the input pin `name`, or for an
		/// output pin listed BEFORE this one (so a texture is sampled once and its channels are
		/// taken from that); {T} for the type the node works in.
		const char* code;
		/// For a node with "any" inputs: how many components its type must have for this pin
		/// to exist - Split's "z" needs 3.
		uint8_t     needs = 0;
	};

	struct NodeType {
		const char*            name;       ///< what a .shadergraph file calls it; never renamed once files use it
		const char*            category;   ///< the menu it is under
		const char*            summary;    ///< one line for the tooltip
		std::vector<InputPin>  inputs;
		std::vector<OutputPin> outputs;
		/// For a node with "any" inputs: the fewest components its type may have - 2 for
		/// Normalize, which needs a vector.
		uint8_t                minComponents = 0;

		int FindInput(std::string_view pin) const;    ///< its place in `inputs`, or -1
		int FindOutput(std::string_view pin) const;   ///< its place in `outputs`, or -1
	};

	/// The node every surface graph ends in: its input pins are the surface (albedo, normal ...).
	inline constexpr const char* kOutputNode = "Output";
	/// A node that reads one of the graph's parameters, named by its "parameter" setting. Its
	/// output has the parameter's type; a texture parameter gives a texture.
	inline constexpr const char* kParameterNode = "Parameter";
	inline constexpr const char* kParameterSetting = "parameter";

	/// Whether a node of this type reads one of the material's built-in controls (Albedo Tint,
	/// Metallic, Roughness, Emissive Factor). A graph with none makes no use of those controls.
	bool IsMaterialFactorNode(std::string_view type);

	/// Every row, in menu order.
	const std::vector<NodeType>& NodeTypes();

	/// The row called `name`, or null.
	const NodeType* FindNodeType(std::string_view name);

	/// Checks the table against itself: names unique, every {placeholder} a pin of its row.
	/// False, and what is wrong, for a row that was added with a mistake. (For the tests.)
	bool CheckNodeTable(std::string& error);

}

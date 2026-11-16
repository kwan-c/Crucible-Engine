/*!
* @file			ShaderGraph.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		The shader graph's data and its file (.shadergraph, JSON) - see ShaderGraph.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraph.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace Crucible::ShaderGraph {

	namespace {

		struct TypeName { ParameterType type; const char* name; int components; };
		constexpr std::array<TypeName, 6> kParameterTypes{ {
			{ ParameterType::Float,   "float",   1 },
			{ ParameterType::Vec2,    "vec2",    2 },
			{ ParameterType::Vec3,    "vec3",    3 },
			{ ParameterType::Vec4,    "vec4",    4 },
			{ ParameterType::Colour,  "colour",  3 },
			{ ParameterType::Texture, "texture", 0 },
		} };

		int Components(ParameterType type) {
			for (const TypeName& known : kParameterTypes) {
				if (known.type == type) { return known.components; }
			}
			return 1;
		}

		// The line (first = 1) that character `offset` of `text` is on.
		size_t LineOf(std::string_view text, size_t offset) {
			return 1 + static_cast<size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>((std::min)(offset, text.size())), '\n'));
		}

		// A number as a float; false if the value is not a number (or not one a float can hold).
		bool ReadNumber(const rapidjson::Value& value, float& out) {
			if (!value.IsNumber()) { return false; }
			const double number = value.GetDouble();
			if (!std::isfinite(number)) { return false; }
			out = static_cast<float>(number);
			return std::isfinite(out);
		}

		// A number, or an array of one to four numbers, into `out` from its first element on.
		// What the value does not give stays as it was.
		bool ReadNumbers(const rapidjson::Value& value, std::array<float, 4>& out) {
			if (value.IsNumber()) { return ReadNumber(value, out[0]); }
			if (!value.IsArray() || value.Size() == 0 || value.Size() > 4) { return false; }
			for (rapidjson::SizeType i = 0; i < value.Size(); ++i) {
				if (!ReadNumber(value[i], out[i])) { return false; }
			}
			return true;
		}

		std::string Text(const rapidjson::Value& value) {
			return std::string(value.GetString(), value.GetStringLength());
		}

		// One end of a link: [node id, "pin name"].
		bool ReadLinkEnd(const rapidjson::Value& value, uint32_t& node, std::string& pin) {
			if (!value.IsArray() || value.Size() != 2 || !value[0].IsUint() || !value[1].IsString()) { return false; }
			node = value[0].GetUint();
			pin = Text(value[1]);
			return node != 0 && !pin.empty();
		}

		// Text as a JSON string, quotes included.
		std::string Quoted(std::string_view text) {
			std::string quoted = "\"";
			for (const char c : text) {
				switch (c) {
				case '"':  quoted += "\\\""; break;
				case '\\': quoted += "\\\\"; break;
				case '\n': quoted += "\\n"; break;
				case '\r': quoted += "\\r"; break;
				case '\t': quoted += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) {
						constexpr char kHex[] = "0123456789abcdef";
						quoted += "\\u00";
						quoted += kHex[(c >> 4) & 0xF];
						quoted += kHex[c & 0xF];
					} else {
						quoted += c;
					}
				}
			}
			return quoted + "\"";
		}

		// [1.0, 0.5, 0.2]
		std::string NumbersText(const std::array<float, 4>& values, int count) {
			std::string text = "[";
			for (int i = 0; i < count; ++i) { text += (i == 0 ? "" : ", ") + NumberText(values[static_cast<size_t>(i)]); }
			return text + "]";
		}

		// A list as lines: one item per line between [ and ], or [] when empty.
		std::string ListText(const std::vector<std::string>& items) {
			if (items.empty()) { return "[]"; }
			std::string text = "[\n";
			for (size_t i = 0; i < items.size(); ++i) { text += "    " + items[i] + (i + 1 < items.size() ? ",\n" : "\n"); }
			return text + "  ]";
		}

	}   // anonymous namespace

	const char* ParameterTypeName(ParameterType type) {
		for (const TypeName& known : kParameterTypes) {
			if (known.type == type) { return known.name; }
		}
		return "float";
	}

	bool IsIdentifier(std::string_view name) {
		if (name.empty() || (name.front() >= '0' && name.front() <= '9')) { return false; }
		return std::all_of(name.begin(), name.end(), [](char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		});
	}

	uint32_t Graph::AddNode(std::string type, float x, float y) {
		Node node;
		node.id = (std::max)(nextId, 1u);
		node.type = std::move(type);
		node.x = x;
		node.y = y;
		nextId = node.id + 1;
		nodes.push_back(std::move(node));
		return nodes.back().id;
	}

	Node* Graph::FindNode(uint32_t id) {
		const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const Node& node) { return node.id == id; });
		return found != nodes.end() ? &*found : nullptr;
	}

	const Node* Graph::FindNode(uint32_t id) const {
		const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const Node& node) { return node.id == id; });
		return found != nodes.end() ? &*found : nullptr;
	}

	const Parameter* Graph::FindParameter(std::string_view name) const {
		const auto found = std::find_if(parameters.begin(), parameters.end(), [name](const Parameter& p) { return p.name == name; });
		return found != parameters.end() ? &*found : nullptr;
	}

	const Link* Graph::FindLinkInto(uint32_t node, std::string_view pin) const {
		const auto found = std::find_if(links.begin(), links.end(), [&](const Link& link) { return link.toNode == node && link.toPin == pin; });
		return found != links.end() ? &*found : nullptr;
	}

	bool Load(std::string_view json, Graph& out, std::string& error) {
		// Some text editors put a UTF-8 mark in front of what they save; JSON has no such thing.
		if (json.starts_with("\xEF\xBB\xBF")) { json.remove_prefix(3); }

		// Comments and a comma after the last item are let through: these files are sometimes
		// written by hand.
		rapidjson::Document doc;
		doc.Parse<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(json.data(), json.size());
		if (doc.HasParseError()) {
			error = "not valid JSON (line " + std::to_string(LineOf(json, doc.GetErrorOffset())) + ": " +
			        rapidjson::GetParseError_En(doc.GetParseError()) + ")";
			return false;
		}
		if (!doc.IsObject()) {
			error = "not a shader graph: the file is not a JSON object";
			return false;
		}

		const auto version = doc.FindMember("version");
		if (version == doc.MemberEnd() || !version->value.IsInt() || version->value.GetInt() < 1) {
			error = "not a shader graph: it has no \"version\"";
			return false;
		}
		if (version->value.GetInt() > kFormatVersion) {
			error = "it was saved by a newer Editor (format " + std::to_string(version->value.GetInt()) +
			        "; this one reads up to " + std::to_string(kFormatVersion) + ")";
			return false;
		}

		Graph graph;
		if (const auto domain = doc.FindMember("domain"); domain != doc.MemberEnd()) {
			if (!domain->value.IsString() || Text(domain->value) != kSurfaceDomain) {
				error = "its \"domain\" is not \"surface\", the only kind of graph there is";
				return false;
			}
		}

		if (const auto parameters = doc.FindMember("parameters"); parameters != doc.MemberEnd()) {
			if (!parameters->value.IsArray()) {
				error = "\"parameters\" is not a list";
				return false;
			}
			for (rapidjson::SizeType i = 0; i < parameters->value.Size(); ++i) {
				const rapidjson::Value& entry = parameters->value[i];
				const std::string where = "parameter " + std::to_string(i + 1);
				if (!entry.IsObject()) {
					error = where + " is not an object";
					return false;
				}
				const auto name = entry.FindMember("name");
				if (name == entry.MemberEnd() || !name->value.IsString()) {
					error = where + " has no \"name\"";
					return false;
				}

				Parameter parameter;
				parameter.name = Text(name->value);
				if (const auto type = entry.FindMember("type"); type != entry.MemberEnd()) {
					const std::string text = type->value.IsString() ? Text(type->value) : std::string();
					const auto known = std::find_if(kParameterTypes.begin(), kParameterTypes.end(),
						[&](const TypeName& t) { return text == t.name; });
					if (known == kParameterTypes.end()) {
						error = "parameter \"" + parameter.name + "\": \"" + text + "\" is not a type (float, vec2, vec3, vec4, colour, texture)";
						return false;
					}
					parameter.type = known->type;
				}

				if (parameter.type == ParameterType::Texture) {
					if (const auto colour = entry.FindMember("colour"); colour != entry.MemberEnd() && colour->value.IsBool()) {
						parameter.textureIsColour = colour->value.GetBool();
					}
					if (const auto fallback = entry.FindMember("default"); fallback != entry.MemberEnd() && fallback->value.IsString()) {
						parameter.textureDefault = Text(fallback->value);
					}
				} else {
					// A colour that is not given a default is white; a number, 0.
					if (parameter.type == ParameterType::Colour) { parameter.defaultValue = { 1.0f, 1.0f, 1.0f, 1.0f }; }
					if (const auto value = entry.FindMember("default"); value != entry.MemberEnd() && !ReadNumbers(value->value, parameter.defaultValue)) {
						error = "parameter \"" + parameter.name + "\": its default is not a number or a list of up to four";
						return false;
					}
					if (const auto low = entry.FindMember("min"); low != entry.MemberEnd()) { ReadNumber(low->value, parameter.minValue); }
					if (const auto high = entry.FindMember("max"); high != entry.MemberEnd()) { ReadNumber(high->value, parameter.maxValue); }
				}
				graph.parameters.push_back(std::move(parameter));
			}
		}

		uint32_t highestId = 0;
		if (const auto nodes = doc.FindMember("nodes"); nodes != doc.MemberEnd()) {
			if (!nodes->value.IsArray()) {
				error = "\"nodes\" is not a list";
				return false;
			}
			std::set<uint32_t> ids;
			for (rapidjson::SizeType i = 0; i < nodes->value.Size(); ++i) {
				const rapidjson::Value& entry = nodes->value[i];
				const std::string where = "node " + std::to_string(i + 1);
				if (!entry.IsObject()) {
					error = where + " is not an object";
					return false;
				}
				const auto id = entry.FindMember("id");
				if (id == entry.MemberEnd() || !id->value.IsUint() || id->value.GetUint() == 0) {
					error = where + " has no \"id\" (a whole number above 0)";
					return false;
				}
				const auto type = entry.FindMember("type");
				if (type == entry.MemberEnd() || !type->value.IsString() || type->value.GetStringLength() == 0) {
					error = where + " (id " + std::to_string(id->value.GetUint()) + ") has no \"type\"";
					return false;
				}

				Node node;
				node.id = id->value.GetUint();
				node.type = Text(type->value);
				if (!ids.insert(node.id).second) {
					error = "two nodes have the id " + std::to_string(node.id);
					return false;
				}
				highestId = (std::max)(highestId, node.id);

				if (const auto x = entry.FindMember("x"); x != entry.MemberEnd()) { ReadNumber(x->value, node.x); }
				if (const auto y = entry.FindMember("y"); y != entry.MemberEnd()) { ReadNumber(y->value, node.y); }

				if (const auto inputs = entry.FindMember("inputs"); inputs != entry.MemberEnd() && inputs->value.IsObject()) {
					for (auto pin = inputs->value.MemberBegin(); pin != inputs->value.MemberEnd(); ++pin) {
						std::array<float, 4> values{};
						if (!ReadNumbers(pin->value, values)) {
							error = "node " + std::to_string(node.id) + ": the value of \"" + Text(pin->name) + "\" is not a number or a list of up to four";
							return false;
						}
						node.inputs[Text(pin->name)] = values;
					}
				}
				if (const auto settings = entry.FindMember("settings"); settings != entry.MemberEnd() && settings->value.IsObject()) {
					for (auto setting = settings->value.MemberBegin(); setting != settings->value.MemberEnd(); ++setting) {
						if (setting->value.IsString()) { node.settings[Text(setting->name)] = Text(setting->value); }
					}
				}
				graph.nodes.push_back(std::move(node));
			}
		}

		if (const auto links = doc.FindMember("links"); links != doc.MemberEnd()) {
			if (!links->value.IsArray()) {
				error = "\"links\" is not a list";
				return false;
			}
			for (rapidjson::SizeType i = 0; i < links->value.Size(); ++i) {
				const rapidjson::Value& entry = links->value[i];
				Link link;
				bool fine = entry.IsObject();
				if (fine) {
					const auto from = entry.FindMember("from");
					const auto to = entry.FindMember("to");
					fine = from != entry.MemberEnd() && to != entry.MemberEnd() &&
					       ReadLinkEnd(from->value, link.fromNode, link.fromPin) && ReadLinkEnd(to->value, link.toNode, link.toPin);
				}
				if (!fine) {
					error = "link " + std::to_string(i + 1) + " is not { \"from\": [node id, \"pin\"], \"to\": [node id, \"pin\"] }";
					return false;
				}
				graph.links.push_back(std::move(link));
			}
		}

		// Never an id a node already has, whatever the file says.
		graph.nextId = highestId + 1;
		if (const auto nextId = doc.FindMember("nextId"); nextId != doc.MemberEnd() && nextId->value.IsUint()) {
			graph.nextId = (std::max)(graph.nextId, nextId->value.GetUint());
		}

		out = std::move(graph);
		return true;
	}

	std::string NumberText(float value) {
		if (!std::isfinite(value)) { return "0.0"; }
		char text[48] = {};
		const std::to_chars_result result = std::to_chars(text, text + sizeof(text) - 1, value);
		std::string number(text, result.ptr);
		if (number.find_first_of(".e") == std::string::npos) { number += ".0"; }
		return number;
	}

	std::string Save(const Graph& graph) {
		// Written by hand rather than with a JSON writer so that every parameter, node and link
		// is one line: a graph in version control then shows what changed, line by line.
		std::vector<std::string> parameters, nodes, links;

		for (const Parameter& parameter : graph.parameters) {
			std::string line = "{ \"name\": " + Quoted(parameter.name) + ", \"type\": \"" + ParameterTypeName(parameter.type) + "\"";
			if (parameter.type == ParameterType::Texture) {
				line += std::string(", \"colour\": ") + (parameter.textureIsColour ? "true" : "false") + ", \"default\": " + Quoted(parameter.textureDefault);
			} else {
				line += ", \"default\": " + (parameter.type == ParameterType::Float ? NumberText(parameter.defaultValue[0])
				                                                                    : NumbersText(parameter.defaultValue, Components(parameter.type)));
				if (parameter.type != ParameterType::Colour) {
					line += ", \"min\": " + NumberText(parameter.minValue) + ", \"max\": " + NumberText(parameter.maxValue);
				}
			}
			parameters.push_back(line + " }");
		}

		for (const Node& node : graph.nodes) {
			std::string line = "{ \"id\": " + std::to_string(node.id) + ", \"type\": " + Quoted(node.type) +
			                   ", \"x\": " + NumberText(node.x) + ", \"y\": " + NumberText(node.y);
			if (!node.inputs.empty()) {
				line += ", \"inputs\": {";
				bool first = true;
				for (const auto& [pin, values] : node.inputs) {
					// As short as it can be and still read back the same: 0.4, or [1.0, 0.5, 0.2].
					int count = 4;
					while (count > 1 && values[static_cast<size_t>(count - 1)] == 0.0f) { --count; }
					line += (first ? " " : ", ") + Quoted(pin) + ": " + (count == 1 ? NumberText(values[0]) : NumbersText(values, count));
					first = false;
				}
				line += " }";
			}
			if (!node.settings.empty()) {
				line += ", \"settings\": {";
				bool first = true;
				for (const auto& [name, value] : node.settings) {
					line += (first ? " " : ", ") + Quoted(name) + ": " + Quoted(value);
					first = false;
				}
				line += " }";
			}
			nodes.push_back(line + " }");
		}

		for (const Link& link : graph.links) {
			links.push_back("{ \"from\": [" + std::to_string(link.fromNode) + ", " + Quoted(link.fromPin) + "], \"to\": [" +
			                std::to_string(link.toNode) + ", " + Quoted(link.toPin) + "] }");
		}

		return "{\n  \"version\": " + std::to_string(kFormatVersion) + ",\n  \"domain\": " + Quoted(graph.domain) +
		       ",\n  \"nextId\": " + std::to_string(graph.nextId) + ",\n  \"parameters\": " + ListText(parameters) +
		       ",\n  \"nodes\": " + ListText(nodes) + ",\n  \"links\": " + ListText(links) + "\n}\n";
	}

}

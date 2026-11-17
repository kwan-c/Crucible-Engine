/*!
* @file			ShaderGraphView.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Implementation - see ShaderGraphView.h.
*
* Draw() has the same three parts as the State Graph view: find what is under the mouse,
* turn the mouse and keys into edits, then draw. Where each node's box and pins are is worked
* out by one function (Layout), used for both the hit test and the drawing.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraphView.h"

#include "ShaderGraph/ShaderGraphNodes.h"
#include "ShaderGraph/ShaderGraphSubgraphs.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <unordered_map>

namespace Crucible {

	using namespace ShaderGraph;

	namespace {

		// A node's box, in graph units. The header height is GraphCanvas::DrawNode's.
		constexpr float kNodeWidth = 190.0f;
		constexpr float kHeader    = 22.0f;
		constexpr float kRow       = 20.0f;   // one pin
		constexpr float kPad       = 5.0f;    // above the first pin and below the last
		constexpr float kTextScale = 0.85f;   // pin labels, against the node's title
		constexpr float kRerouteWidth  = 34.0f;   // a Reroute: a dot with a pin each side, room between them to grab it
		constexpr float kRerouteHeight = 20.0f;
		constexpr float kCommentBar    = 26.0f;   // a comment box's title strip: drag it to move the box
		constexpr float kCommentGrip   = 14.0f;   // its bottom-right corner: drag it to resize

		constexpr ImU32 kGold     = IM_COL32(255, 215, 110, 255);   // selected and hovered things, as in the other graph views
		constexpr ImU32 kError    = IM_COL32(235, 90, 80, 255);
		constexpr ImU32 kWarning  = IM_COL32(230, 170, 70, 255);
		constexpr ImU32 kLabel    = IM_COL32(215, 220, 228, 255);
		constexpr ImU32 kDimLabel = IM_COL32(150, 156, 168, 255);

		// One node's box on screen, and what the table and the generator say about it.
		struct Box {
			const Node*                  node = nullptr;
			const NodeType*              type = nullptr;      // null: a kind of node this Editor does not have
			const Generated::NodeResult* result = nullptr;    // null: the generator could not type it
			ImVec2                       min{}, max{};
		};

		ImU32 TypeColour(ValueType type) {
			switch (type) {
			case ValueType::Float:   return IM_COL32(175, 185, 200, 255);
			case ValueType::Float2:  return IM_COL32(120, 200, 140, 255);
			case ValueType::Float3:  return IM_COL32(235, 200, 90, 255);
			case ValueType::Float4:  return IM_COL32(230, 130, 185, 255);
			case ValueType::Texture: return IM_COL32(240, 150, 70, 255);
			default:                 return IM_COL32(120, 170, 230, 255);   // not known yet
			}
		}

		ImU32 CategoryColour(std::string_view category) {
			if (category == "Output")   { return IM_COL32(150, 70, 70, 255); }
			if (category == "Input")    { return IM_COL32(40, 130, 90, 255); }
			if (category == "Constant") { return IM_COL32(95, 100, 112, 255); }
			if (category == "Math")     { return IM_COL32(60, 95, 150, 255); }
			if (category == "Colour")   { return IM_COL32(165, 135, 45, 255); }
			if (category == "Vector")   { return IM_COL32(110, 85, 160, 255); }
			if (category == "Texture")  { return IM_COL32(170, 105, 50, 255); }
			if (category == "UV")       { return IM_COL32(30, 130, 140, 255); }
			if (category == "Effect")   { return IM_COL32(160, 80, 130, 255); }
			if (category == "Sub-graph") { return IM_COL32(45, 115, 175, 255); }
			return IM_COL32(95, 100, 112, 255);
		}

		// "SampleTexture" -> "Sample Texture": a node type's name as a title.
		std::string Title(std::string_view name) {
			std::string title;
			for (size_t i = 0; i < name.size(); ++i) {
				const bool upper = name[i] >= 'A' && name[i] <= 'Z';
				if (upper && i > 0 && name[i - 1] >= 'a' && name[i - 1] <= 'z') { title += ' '; }
				title += name[i];
			}
			return title;
		}

		// "Glow.shadersubgraph" -> "Glow": a sub-graph's name as a title.
		std::string SubgraphTitle(const SubgraphEntry& entry) {
			const size_t dot = entry.label.rfind('.');
			return dot == std::string::npos ? entry.label : entry.label.substr(0, dot);
		}

		std::string Lower(std::string_view text) {
			std::string lower(text);
			for (char& c : lower) { c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); }
			return lower;
		}

		// "0.5", "(0.1, 0, 2)": an unconnected pin's constant, short enough to sit beside its name.
		std::string ValueText(ValueType type, const std::array<float, 4>& values) {
			char text[64] = {};
			switch (ComponentCount(type)) {
			case 2:  std::snprintf(text, sizeof(text), "(%.3g, %.3g)", values[0], values[1]); break;
			case 3:  std::snprintf(text, sizeof(text), "(%.3g, %.3g, %.3g)", values[0], values[1], values[2]); break;
			case 4:  std::snprintf(text, sizeof(text), "(%.3g, %.3g, %.3g, %.3g)", values[0], values[1], values[2], values[3]); break;
			default: std::snprintf(text, sizeof(text), "%.3g", values[0]); break;
			}
			return text;
		}

		float DistanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
			const float dx = b.x - a.x, dy = b.y - a.y;
			const float lengthSq = dx * dx + dy * dy;
			const float t = lengthSq > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq, 0.0f, 1.0f) : 0.0f;
			const float cx = a.x + t * dx - p.x, cy = a.y + t * dy - p.y;
			return std::sqrt(cx * cx + cy * cy);
		}

		// A link's curve: out of the right of one node, into the left of another. `bend` is how
		// far its two handles reach sideways.
		float LinkBend(const ImVec2& from, const ImVec2& to, float zoom) {
			return (std::max)(40.0f * zoom, std::abs(to.x - from.x) * 0.5f);
		}

		ImVec2 LinkPoint(const ImVec2& from, const ImVec2& to, float bend, float t) {
			const ImVec2 c1{ from.x + bend, from.y }, c2{ to.x - bend, to.y };
			const float u = 1.0f - t;
			const float a = u * u * u, b = 3.0f * u * u * t, c = 3.0f * u * t * t, d = t * t * t;
			return { a * from.x + b * c1.x + c * c2.x + d * to.x, a * from.y + b * c1.y + c * c2.y + d * to.y };
		}

		// How near the mouse is to a link, in pixels.
		float DistanceToLink(const ImVec2& mouse, const ImVec2& from, const ImVec2& to, float zoom) {
			constexpr int kSteps = 24;
			const float bend = LinkBend(from, to, zoom);
			float nearest = FLT_MAX;
			ImVec2 previous = from;
			for (int i = 1; i <= kSteps; ++i) {
				const ImVec2 point = LinkPoint(from, to, bend, static_cast<float>(i) / kSteps);
				nearest = (std::min)(nearest, DistanceToSegment(mouse, previous, point));
				previous = point;
			}
			return nearest;
		}

		bool IsReroute(const NodeType* type) {
			return type && std::string_view(type->name) == kRerouteNode;
		}

		float NodeWidth(const NodeType* type) {
			return IsReroute(type) ? kRerouteWidth : kNodeWidth;
		}

		float NodeHeight(const NodeType* type) {
			if (IsReroute(type)) { return kRerouteHeight; }
			const size_t rows = type ? (std::max)((std::max)(type->inputs.size(), type->outputs.size()), size_t{ 1 }) : 1;
			return kHeader + kPad * 2.0f + kRow * static_cast<float>(rows);
		}

		ImVec2 PinAt(const Box& box, bool output, size_t index, float zoom) {
			if (IsReroute(box.type)) { return { output ? box.max.x : box.min.x, (box.min.y + box.max.y) * 0.5f }; }
			return { output ? box.max.x : box.min.x, box.min.y + (kHeader + kPad + kRow * (static_cast<float>(index) + 0.5f)) * zoom };
		}

		// The type a pin carries, as the generator worked it out; Any when it could not.
		ValueType PinType(const Box& box, bool output, size_t index) {
			if (output) {
				return box.result && box.result->valid && index < box.result->outputs.size() ? box.result->outputs[index] : ValueType::Any;
			}
			const ValueType declared = box.type->inputs[index].type;
			if (declared != ValueType::Any) { return declared; }
			return box.result && box.result->valid ? box.result->working : ValueType::Any;
		}

	}   // anonymous namespace

	bool ShaderGraphView::IsSelected(uint32_t node) const {
		return std::find(m_selected.begin(), m_selected.end(), node) != m_selected.end();
	}

	void ShaderGraphView::Select(uint32_t node) {
		m_selected.assign(1, node);
		m_hasLink = false;
		m_comment = 0;
	}

	void ShaderGraphView::ClearSelection() {
		m_selected.clear();
		m_hasLink = false;
		m_comment = 0;
	}

	void ShaderGraphView::EditComment(const Graph& graph, uint32_t comment) {
		const Comment* found = graph.FindComment(comment);
		if (!found) { return; }
		m_editComment = comment;
		std::snprintf(m_commentText, sizeof(m_commentText), "%s", found->text.c_str());
		m_openCommentEditor = true;   // opened by DrawMenus, where its popup lives
	}

	bool ShaderGraphView::CommentAroundSelection(Graph& graph) {
		bool any = false;
		Math::vec2 low(0.0f), high(0.0f);
		for (const uint32_t id : m_selected) {
			const Node* node = graph.FindNode(id);
			if (!node) { continue; }
			const NodeType* type = TypeOfNode(graph, *node);
			const Math::vec2 a(node->x, node->y), b(node->x + NodeWidth(type), node->y + NodeHeight(type));
			low = any ? glm::min(low, a) : a;
			high = any ? glm::max(high, b) : b;
			any = true;
		}
		if (!any) { return false; }
		constexpr float kMargin = 20.0f;
		const uint32_t comment = graph.AddComment(low.x - kMargin, low.y - kMargin - kCommentBar,
		                                          high.x - low.x + kMargin * 2.0f, high.y - low.y + kMargin * 2.0f + kCommentBar, "Comment");
		ClearSelection();
		m_comment = comment;
		EditComment(graph, comment);   // straight on to its title
		return true;
	}

	void ShaderGraphView::KeepSelectionIn(const Graph& graph) {
		std::erase_if(m_selected, [&](uint32_t id) { return graph.FindNode(id) == nullptr; });
		if (m_comment != 0 && !graph.FindComment(m_comment)) { m_comment = 0; }
		m_commentDragging = m_commentResizing = false;
		if (m_hasLink) {
			const Link* now = graph.FindLinkInto(m_link.toNode, m_link.toPin);
			m_hasLink = now && now->fromNode == m_link.fromNode && now->fromPin == m_link.fromPin;
		}
		m_dragging = m_linking = m_boxSelecting = false;
	}

	void ShaderGraphView::FrameAll(const Graph& graph) {
		if (graph.nodes.empty()) { return; }
		Math::vec2 low(graph.nodes.front().x, graph.nodes.front().y), high = low;
		for (const Node& node : graph.nodes) {
			const NodeType* type = TypeOfNode(graph, node);
			low = glm::min(low, Math::vec2(node.x, node.y));
			high = glm::max(high, Math::vec2(node.x + NodeWidth(type), node.y + NodeHeight(type)));
		}
		for (const Comment& comment : graph.comments) {
			low = glm::min(low, Math::vec2(comment.x, comment.y));
			high = glm::max(high, Math::vec2(comment.x + comment.width, comment.y + comment.height));
		}
		m_canvas.FrameBox(low, high);
	}

	bool ShaderGraphView::LinkNewNode(Graph& graph, uint32_t node) {
		const Node* target = graph.FindNode(node);
		const NodeType* type = target ? TypeOfNode(graph, *target) : nullptr;
		if (!type) { return false; }
		std::string why;
		if (m_linkFromOutput) {
			// Onto the first input that takes it; one with nothing linked yet, if there is one.
			for (const bool freeOnly : { true, false }) {
				for (const InputPin& pin : type->inputs) {
					if (freeOnly && graph.FindLinkInto(node, pin.name)) { continue; }
					if (Connect(graph, m_linkNode, m_linkPin, node, pin.name, why)) { return true; }
				}
			}
			return false;
		}
		for (const OutputPin& pin : type->outputs) {
			if (Connect(graph, node, pin.name, m_linkNode, m_linkPin, why)) { return true; }
		}
		return false;
	}

	void ShaderGraphView::Duplicate(Graph& graph) {
		const Clipboard copy = Copy(graph, m_selected);
		if (copy.Empty()) { return; }
		float left = copy.nodes.front().x, top = copy.nodes.front().y;
		for (const Node& node : copy.nodes) {
			left = (std::min)(left, node.x);
			top = (std::min)(top, node.y);
		}
		m_selected = Paste(graph, copy, left + 30.0f, top + 30.0f);
		m_hasLink = false;
	}

	ShaderGraphView::Result ShaderGraphView::Draw(Graph& graph, const Generated& check, const std::vector<Problem>& problems, bool readOnly) {
		Result result;
		m_canvas.Begin("##ShaderGraphCanvas");
		const float zoom = m_canvas.Zoom();
		{
			const ImVec2 low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();   // the canvas's own item
			m_viewCentre = m_canvas.ToGraph({ (low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f });
		}

		std::unordered_map<uint32_t, const Generated::NodeResult*> results;
		for (const Generated::NodeResult& nodeResult : check.nodes) { results[nodeResult.node] = &nodeResult; }

		// Where every node is this frame. Called again after the edits below.
		std::vector<Box> boxes;
		std::unordered_map<uint32_t, size_t> boxOf;
		const auto layout = [&] {
			boxes.clear();
			boxOf.clear();
			for (const Node& node : graph.nodes) {
				Box box;
				box.node = &node;
				box.type = TypeOfNode(graph, node);
				if (const auto found = results.find(node.id); found != results.end()) { box.result = found->second; }
				box.min = m_canvas.ToScreen({ node.x, node.y });
				box.max = { box.min.x + NodeWidth(box.type) * zoom, box.min.y + NodeHeight(box.type) * zoom };
				boxOf[node.id] = boxes.size();
				boxes.push_back(box);
			}
		};
		// A link's two ends on screen. False if a node or pin of it is not there.
		const auto linkEnds = [&](const Link& link, ImVec2& from, ImVec2& to, ValueType* type = nullptr) {
			const auto source = boxOf.find(link.fromNode), target = boxOf.find(link.toNode);
			if (source == boxOf.end() || target == boxOf.end()) { return false; }
			const Box& a = boxes[source->second];
			const Box& b = boxes[target->second];
			const int out = a.type ? a.type->FindOutput(link.fromPin) : -1;
			const int in = b.type ? b.type->FindInput(link.toPin) : -1;
			if (out < 0 || in < 0) { return false; }
			from = PinAt(a, true, static_cast<size_t>(out), zoom);
			to = PinAt(b, false, static_cast<size_t>(in), zoom);
			if (type) { *type = PinType(a, true, static_cast<size_t>(out)); }
			return true;
		};
		layout();

		// ---- 1. What is under the mouse: a pin, a node, or a link ---------------------------------
		const ImVec2 mouse = m_canvas.Mouse();
		const float  reach = (std::max)(7.0f, 9.0f * zoom);
		uint32_t hoveredNode = 0;
		uint32_t pinNode = 0;         // the node of the hovered pin (0 = no pin)
		bool     pinIsOutput = false;
		size_t   pinIndex = 0;
		bool     overLink = false;    // a link is under the mouse: `hoveredLink` (a copy - the edits below move the links about)
		Link     hoveredLink;
		if (m_canvas.Hovered()) {
			for (const Box& box : boxes) {   // later nodes are drawn on top, so they win
				if (mouse.x >= box.min.x && mouse.y >= box.min.y && mouse.x <= box.max.x && mouse.y <= box.max.y) { hoveredNode = box.node->id; }
				if (!box.type) { continue; }
				for (const bool output : { false, true }) {
					const size_t count = output ? box.type->outputs.size() : box.type->inputs.size();
					for (size_t i = 0; i < count; ++i) {
						const ImVec2 pin = PinAt(box, output, i, zoom);
						if ((mouse.x - pin.x) * (mouse.x - pin.x) + (mouse.y - pin.y) * (mouse.y - pin.y) <= reach * reach) {
							pinNode = box.node->id;
							pinIsOutput = output;
							pinIndex = i;
						}
					}
				}
			}
			if (hoveredNode == 0 && pinNode == 0) {
				for (const Link& link : graph.links) {
					ImVec2 from, to;
					if (linkEnds(link, from, to) && DistanceToLink(mouse, from, to, zoom) < 6.0f) {
						overLink = true;
						hoveredLink = link;
					}
				}
			}
		}
		// A comment box's title strip or resize corner - only where nothing else is: a comment lies
		// under everything, and its inside is empty canvas (drag there to box-select).
		uint32_t commentBar = 0, commentGrip = 0;
		if (m_canvas.Hovered() && hoveredNode == 0 && pinNode == 0 && !overLink) {
			const Math::vec2 at = m_canvas.MouseGraph();
			const float grip = (std::max)(kCommentGrip, 10.0f / zoom);   // never under 10 pixels
			for (auto it = graph.comments.rbegin(); it != graph.comments.rend(); ++it) {   // the one on top first
				const Comment& comment = *it;
				if (at.x < comment.x || at.y < comment.y || at.x > comment.x + comment.width || at.y > comment.y + comment.height) { continue; }
				if (at.x >= comment.x + comment.width - grip && at.y >= comment.y + comment.height - grip) { commentGrip = comment.id; break; }
				if (at.y <= comment.y + kCommentBar) { commentBar = comment.id; break; }
			}
		}

		const auto pinName = [&]() -> std::string {
			const NodeType* type = boxes[boxOf[pinNode]].type;
			return pinIsOutput ? type->outputs[pinIndex].name : type->inputs[pinIndex].name;
		};

		// ---- 2. The mouse and keys, turned into edits ---------------------------------------------
		const ImGuiIO& io = ImGui::GetIO();
		if (m_canvas.LeftClicked()) {
			if (commentBar == 0 && commentGrip == 0) { m_comment = 0; }
			if (pinNode != 0 && !readOnly) {
				m_linking = true;
				m_linkDetached = false;
				m_linkFromOutput = pinIsOutput;
				m_linkNode = pinNode;
				m_linkPin = pinName();
				if (!pinIsOutput) {
					// An input that already has a link: take hold of that link's far end instead,
					// so it can be moved to another pin or dropped to remove it.
					if (const Link* existing = graph.FindLinkInto(pinNode, m_linkPin)) {
						m_linkFromOutput = true;
						m_linkNode = existing->fromNode;
						const std::string fromPin = existing->fromPin;
						DeleteLinkInto(graph, pinNode, m_linkPin);
						m_linkPin = fromPin;
						m_linkDetached = true;
						m_hasLink = false;
					}
				}
			} else if (hoveredNode != 0 && io.KeyShift) {
				m_hasLink = false;
				const auto it = std::find(m_selected.begin(), m_selected.end(), hoveredNode);
				if (it != m_selected.end()) { m_selected.erase(it); }
				else { m_selected.push_back(hoveredNode); m_dragging = !readOnly; m_moved = false; }
			} else if (hoveredNode != 0) {
				// A click on a node already selected keeps the selection: the drag moves them all.
				if (!IsSelected(hoveredNode)) { Select(hoveredNode); }
				m_hasLink = false;
				m_dragging = !readOnly;
				m_moved = false;
			} else if (overLink) {
				m_selected.clear();
				m_link = hoveredLink;
				m_hasLink = true;
				// A double-click puts a reroute point there, to bend the link around something.
				if (!readOnly && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					const Math::vec2 at = m_canvas.MouseGraph();
					const uint32_t reroute = InsertReroute(graph, hoveredLink.toNode, hoveredLink.toPin,
					                                       at.x - kRerouteWidth * 0.5f, at.y - kRerouteHeight * 0.5f, result.refused);
					if (reroute != 0) {
						result.changed = true;
						Select(reroute);
					}
				}
			} else if (commentBar != 0 || commentGrip != 0) {
				m_selected.clear();
				m_hasLink = false;
				m_comment = commentGrip != 0 ? commentGrip : commentBar;
				m_moved = false;
				if (!readOnly && commentGrip != 0) {
					m_commentResizing = true;
				} else if (!readOnly && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					EditComment(graph, m_comment);
				} else if (!readOnly) {
					// The nodes inside the box now move with it.
					m_commentDragging = true;
					m_commentNodes.clear();
					if (const Comment* comment = graph.FindComment(m_comment)) {
						for (const Node& node : graph.nodes) {
							const NodeType* type = TypeOfNode(graph, node);
							if (node.x >= comment->x && node.y >= comment->y && node.x + NodeWidth(type) <= comment->x + comment->width &&
							    node.y + NodeHeight(type) <= comment->y + comment->height) {
								m_commentNodes.push_back(node.id);
							}
						}
					}
				}
			} else {
				if (!io.KeyShift) { ClearSelection(); }
				m_boxSelecting = true;
				m_boxStart = m_canvas.MouseGraph();
			}
		}

		if (m_dragging && m_canvas.LeftHeld() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
			for (const uint32_t id : m_selected) {
				if (Node* node = graph.FindNode(id)) {
					node->x += io.MouseDelta.x / zoom;
					node->y += io.MouseDelta.y / zoom;
				}
			}
			m_moved = true;
		}
		if ((m_commentDragging || m_commentResizing) && m_canvas.LeftHeld() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
			if (Comment* comment = graph.FindComment(m_comment)) {
				const float dx = io.MouseDelta.x / zoom, dy = io.MouseDelta.y / zoom;
				if (m_commentResizing) {
					comment->width  = (std::max)(comment->width + dx, kMinCommentSize);
					comment->height = (std::max)(comment->height + dy, kMinCommentSize);
				} else {
					comment->x += dx;
					comment->y += dy;
					for (const uint32_t id : m_commentNodes) {
						if (Node* node = graph.FindNode(id)) {
							node->x += dx;
							node->y += dy;
						}
					}
				}
				m_moved = true;
			}
		}

		if (m_canvas.LeftReleased()) {
			if (m_commentDragging || m_commentResizing) {
				m_commentDragging = m_commentResizing = false;
				if (m_moved) { result.changed = true; }
			}
			if (m_dragging) {
				m_dragging = false;
				if (m_moved) { result.changed = true; }
			}
			if (m_linking) {
				m_linking = false;
				if (m_linkDetached) { result.changed = true; }
				if (pinNode != 0 && pinIsOutput != m_linkFromOutput) {
					const std::string other = pinName();
					const bool linked = m_linkFromOutput ? Connect(graph, m_linkNode, m_linkPin, pinNode, other, result.refused)
					                                     : Connect(graph, pinNode, other, m_linkNode, m_linkPin, result.refused);
					if (linked) { result.changed = true; }
				} else if (hoveredNode != 0 && hoveredNode != m_linkNode && pinNode == 0) {
					if (LinkNewNode(graph, hoveredNode)) { result.changed = true; }
					else { result.refused = "None of that node's pins takes this link."; }
				} else if (hoveredNode == 0 && pinNode == 0 && m_canvas.Hovered()) {
					// Dropped on empty canvas: choose a node to make there, already linked.
					m_menuPosition = m_canvas.MouseGraph();
					m_menuLinks = true;
					m_filter[0] = '\0';
					ImGui::OpenPopup("ShaderAddMenu");
				}
			}
			if (m_boxSelecting) {
				m_boxSelecting = false;
				const Math::vec2 end = m_canvas.MouseGraph();
				const Math::vec2 low = glm::min(m_boxStart, end), high = glm::max(m_boxStart, end);
				if (high.x - low.x > 2.0f || high.y - low.y > 2.0f) {   // a click is not a box
					for (const Node& node : graph.nodes) {
						const NodeType* type = TypeOfNode(graph, node);
						const float bottom = node.y + NodeHeight(type);
						if (node.x <= high.x && node.x + NodeWidth(type) >= low.x && node.y <= high.y && bottom >= low.y && !IsSelected(node.id)) {
							m_selected.push_back(node.id);
						}
					}
				}
			}
		}

		if (m_canvas.RightClicked() && !readOnly) {
			if (hoveredNode != 0) {
				if (!IsSelected(hoveredNode)) { Select(hoveredNode); }
				ImGui::OpenPopup("ShaderNodeMenu");
			} else if (overLink) {
				m_selected.clear();
				m_comment = 0;
				m_link = hoveredLink;
				m_hasLink = true;
				m_menuPosition = m_canvas.MouseGraph();   // where "Add reroute point" puts it
				ImGui::OpenPopup("ShaderLinkMenu");
			} else if (commentBar != 0 || commentGrip != 0) {
				m_selected.clear();
				m_hasLink = false;
				m_comment = commentBar != 0 ? commentBar : commentGrip;
				ImGui::OpenPopup("ShaderCommentMenu");
			} else {
				m_menuPosition = m_canvas.MouseGraph();
				m_menuLinks = false;
				m_filter[0] = '\0';
				ImGui::OpenPopup("ShaderAddMenu");
			}
		}

		// Keys - claimed with Shortcut, so the Hierarchy's global ones stay out of it.
		if (!readOnly && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
			if (ImGui::Shortcut(ImGuiKey_Delete)) {
				if (!m_selected.empty()) {
					if (DeleteNodes(graph, m_selected) != 0) { result.changed = true; }
					ClearSelection();
				} else if (m_comment != 0) {
					if (DeleteComments(graph, { m_comment }) != 0) { result.changed = true; }
					m_comment = 0;
				} else if (m_hasLink) {
					if (DeleteLinkInto(graph, m_link.toNode, m_link.toPin)) { result.changed = true; }
					m_hasLink = false;
				}
			}
			// C: a comment box around the selected nodes.
			if (ImGui::Shortcut(ImGuiKey_C) && !m_selected.empty() && CommentAroundSelection(graph)) { result.changed = true; }
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C) && !m_selected.empty()) { m_clipboard = Copy(graph, m_selected); }
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V) && !m_clipboard.Empty()) {
				// At the mouse if it is over the canvas; else in the middle of the view.
				const Math::vec2 where = m_canvas.Hovered() ? m_canvas.MouseGraph() : m_viewCentre;
				m_selected = Paste(graph, m_clipboard, where.x, where.y);
				m_hasLink = false;
				result.changed = true;
			}
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D) && !m_selected.empty()) {
				Duplicate(graph);
				result.changed = true;
			}
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
				m_selected.clear();
				m_hasLink = false;
				for (const Node& node : graph.nodes) { m_selected.push_back(node.id); }
			}
		}

		// ---- 3. Draw: links under the nodes ------------------------------------------------------
		layout();   // the edits above may have added, removed or moved nodes
		ImDrawList* draw = m_canvas.DrawList();

		// Comment boxes, under everything: a tinted box, a stronger title strip, a resize corner.
		for (const Comment& comment : graph.comments) {
			const ImVec2 low = m_canvas.ToScreen({ comment.x, comment.y });
			const ImVec2 high = m_canvas.ToScreen({ comment.x + comment.width, comment.y + comment.height });
			const ImVec2 barHigh{ high.x, (std::min)(low.y + kCommentBar * zoom, high.y) };
			const auto channel = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
			const int red = channel(comment.colour[0]), green = channel(comment.colour[1]), blue = channel(comment.colour[2]);
			const bool selected = comment.id == m_comment;
			const bool hovered = comment.id == commentBar || comment.id == commentGrip;
			const float rounding = 6.0f * zoom;
			draw->AddRectFilled(low, high, IM_COL32(red, green, blue, 40), rounding);
			draw->AddRectFilled(low, barHigh, IM_COL32(red, green, blue, 150), rounding, ImDrawFlags_RoundCornersTop);
			draw->AddRect(low, high, selected ? kGold : IM_COL32(red, green, blue, hovered ? 230 : 150), rounding, 0, selected ? 2.5f : 1.5f);
			const float titleSize = ImGui::GetFontSize() * zoom;
			const ImVec4 titleClip{ low.x, low.y, high.x - 4.0f * zoom, barHigh.y };
			draw->AddText(ImGui::GetFont(), titleSize, { low.x + 8.0f * zoom, low.y + (kCommentBar * zoom - titleSize) * 0.5f }, kLabel,
			              comment.text.c_str(), nullptr, 0.0f, &titleClip);
			const float corner = kCommentGrip * zoom;
			draw->AddTriangleFilled({ high.x - corner, high.y }, { high.x, high.y - corner }, high,
			                        IM_COL32(red, green, blue, comment.id == commentGrip ? 255 : 150));
		}

		for (const Link& link : graph.links) {
			ImVec2 from, to;
			ValueType type = ValueType::Any;
			if (!linkEnds(link, from, to, &type)) { continue; }
			const bool selected = m_hasLink && link.toNode == m_link.toNode && link.toPin == m_link.toPin;
			const bool hovered = overLink && link.toNode == hoveredLink.toNode && link.toPin == hoveredLink.toPin;
			const float bend = LinkBend(from, to, zoom);
			draw->AddBezierCubic(from, { from.x + bend, from.y }, { to.x - bend, to.y }, to,
			                     selected ? kGold : TypeColour(type), (selected ? 3.5f : hovered ? 3.0f : 2.0f) * (std::max)(zoom, 0.6f));
		}
		if (m_linking) {
			if (const auto fixed = boxOf.find(m_linkNode); fixed != boxOf.end() && boxes[fixed->second].type) {
				const Box& box = boxes[fixed->second];
				const int index = m_linkFromOutput ? box.type->FindOutput(m_linkPin) : box.type->FindInput(m_linkPin);
				if (index >= 0) {
					const ImVec2 pin = PinAt(box, m_linkFromOutput, static_cast<size_t>(index), zoom);
					const ImVec2 from = m_linkFromOutput ? pin : mouse, to = m_linkFromOutput ? mouse : pin;
					const float bend = LinkBend(from, to, zoom);
					draw->AddBezierCubic(from, { from.x + bend, from.y }, { to.x - bend, to.y }, to, kGold, 2.0f);
				}
			}
		}

		// The nodes: the canvas's box and title, then the pins down both sides.
		const float fontSize = ImGui::GetFontSize() * zoom * kTextScale;
		const float radius = (std::max)(3.5f, 5.0f * zoom);
		std::string hoveredProblems;
		for (const Box& box : boxes) {
			const Node& node = *box.node;
			const bool used = box.result && box.result->used;
			const bool selected = IsSelected(node.id);

			// A reroute point: a dot in the colour of what it carries, a pin each side, no title.
			if (IsReroute(box.type)) {
				bool stops = false, any = false;
				for (const Problem& problem : problems) {
					if (problem.node != node.id) { continue; }
					any = true;
					stops = stops || problem.stops;
					if (hoveredNode == node.id) { hoveredProblems += (hoveredProblems.empty() ? "" : "\n") + problem.message; }
				}
				const ImVec2 centre{ (box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f };
				const ImU32 carried = TypeColour(PinType(box, true, 0));
				const ImU32 fill = used ? carried : (carried & 0x00FFFFFFu) | 0x80000000u;   // faded when the Output does not use it
				const float dot = (std::max)(4.0f, 7.0f * zoom);
				draw->AddLine(PinAt(box, false, 0, zoom), PinAt(box, true, 0, zoom), fill, 2.0f * (std::max)(zoom, 0.6f));
				draw->AddCircleFilled(centre, dot, fill);
				const ImU32 ring = any ? (stops ? kError : kWarning) : selected || hoveredNode == node.id ? kGold : IM_COL32(20, 22, 26, 255);
				draw->AddCircle(centre, dot + 1.5f, ring, 0, selected || any ? 2.5f : 1.5f);
				for (const bool output : { false, true }) {
					const bool hovered = pinNode == node.id && pinIsOutput == output;
					draw->AddCircleFilled(PinAt(box, output, 0, zoom), radius * 0.8f, hovered ? kGold : carried);
				}
				continue;
			}

			std::string title = box.type ? Title(node.type) : node.type + "?";
			if (node.type == kParameterNode) {
				const auto setting = node.settings.find(kParameterSetting);
				title = setting != node.settings.end() && !setting->second.empty() ? setting->second : "(choose a parameter)";
			}
			const bool isSubgraph = node.type == kSubgraphNode;
			if (isSubgraph) {
				const auto setting = node.settings.find(kSubgraphSetting);
				const SubgraphEntry* entry = setting != node.settings.end() ? FindSubgraph(setting->second) : nullptr;
				title = entry ? SubgraphTitle(*entry) : "(missing sub-graph)";
			}
			const ImVec2 size{ box.max.x - box.min.x, box.max.y - box.min.y };
			m_canvas.DrawNode(box.min, size, CategoryColour(isSubgraph ? "Sub-graph" : box.type ? box.type->category : ""), title.c_str(), nullptr, selected, !used);

			// Its problems: an outline, and the text when the mouse is over it.
			bool stops = false, any = false;
			for (const Problem& problem : problems) {
				if (problem.node != node.id) { continue; }
				any = true;
				stops = stops || problem.stops;
				if (hoveredNode == node.id) {
					hoveredProblems += (hoveredProblems.empty() ? "" : "\n") + (problem.pin.empty() ? std::string() : problem.pin + ": ") + problem.message;
				}
			}
			if (any) { draw->AddRect(box.min, box.max, stops ? kError : kWarning, 6.0f * zoom, 0, 2.5f); }
			if (!box.type) { continue; }

			const ImVec4 clip{ box.min.x, box.min.y, box.max.x, box.max.y };
			const float textDrop = fontSize * 0.5f;
			for (size_t i = 0; i < box.type->inputs.size(); ++i) {
				const InputPin& pin = box.type->inputs[i];
				const ImVec2 at = PinAt(box, false, i, zoom);
				const bool linked = graph.FindLinkInto(node.id, pin.name) != nullptr;
				const bool hovered = pinNode == node.id && !pinIsOutput && pinIndex == i;
				draw->AddCircleFilled(at, radius, hovered ? kGold : linked ? TypeColour(PinType(box, false, i)) : IM_COL32(52, 56, 64, 255));
				draw->AddCircle(at, radius, hovered ? kGold : TypeColour(PinType(box, false, i)), 0, 1.5f);

				// Its name, then - with nothing linked - what it is worth.
				ImVec2 text{ at.x + 9.0f * zoom, at.y - textDrop };
				draw->AddText(ImGui::GetFont(), fontSize, text, kLabel, pin.name, nullptr, 0.0f, &clip);
				if (linked || pin.defaultCode) { continue; }
				text.x += ImGui::CalcTextSize(pin.name).x * zoom * kTextScale + 7.0f * zoom;
				if (pin.type == ValueType::Texture) {
					draw->AddText(ImGui::GetFont(), fontSize, text, kError, "(none)", nullptr, 0.0f, &clip);
					continue;
				}
				const auto constant = node.inputs.find(pin.name);
				const std::array<float, 4>& values = constant != node.inputs.end() ? constant->second : pin.defaultValue;
				if (pin.colour) {
					const float side = fontSize * 0.9f;
					const ImVec2 corner{ text.x, at.y - side * 0.5f };
					const auto channel = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
					draw->AddRectFilled(corner, { corner.x + side * 1.6f, corner.y + side }, IM_COL32(channel(values[0]), channel(values[1]), channel(values[2]), 255), 2.0f);
					draw->AddRect(corner, { corner.x + side * 1.6f, corner.y + side }, IM_COL32(20, 22, 26, 255), 2.0f);
				} else {
					const std::string value = ValueText(pin.type == ValueType::Any ? ValueType::Float : pin.type, values);
					draw->AddText(ImGui::GetFont(), fontSize, text, kDimLabel, value.c_str(), nullptr, 0.0f, &clip);
				}
			}
			for (size_t i = 0; i < box.type->outputs.size(); ++i) {
				const OutputPin& pin = box.type->outputs[i];
				const ImVec2 at = PinAt(box, true, i, zoom);
				const bool hovered = pinNode == node.id && pinIsOutput && pinIndex == i;
				draw->AddCircleFilled(at, radius, hovered ? kGold : TypeColour(PinType(box, true, i)));
				draw->AddCircle(at, radius, IM_COL32(20, 22, 26, 255), 0, 1.5f);
				const float width = ImGui::CalcTextSize(pin.name).x * zoom * kTextScale;
				draw->AddText(ImGui::GetFont(), fontSize, { at.x - 9.0f * zoom - width, at.y - textDrop }, kLabel, pin.name, nullptr, 0.0f, &clip);
			}
		}

		if (m_boxSelecting) {
			const ImVec2 a = m_canvas.ToScreen(m_boxStart);
			const ImVec2 low{ (std::min)(a.x, mouse.x), (std::min)(a.y, mouse.y) }, high{ (std::max)(a.x, mouse.x), (std::max)(a.y, mouse.y) };
			draw->AddRectFilled(low, high, IM_COL32(255, 215, 110, 30));
			draw->AddRect(low, high, IM_COL32(255, 215, 110, 160));
		}

		m_canvas.End();

		// What the mouse is over, in words.
		if (!m_dragging && !m_boxSelecting && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
			if (pinNode != 0) {
				if (const auto found = boxOf.find(pinNode); found != boxOf.end() && boxes[found->second].type) {
					const Box& box = boxes[found->second];
					const size_t count = pinIsOutput ? box.type->outputs.size() : box.type->inputs.size();
					if (pinIndex < count) {
						const ValueType type = PinType(box, pinIsOutput, pinIndex);
						ImGui::SetTooltip("%s: %s", pinIsOutput ? box.type->outputs[pinIndex].name : box.type->inputs[pinIndex].name,
						                  type == ValueType::Any ? "any number or vector" : ValueTypeName(type));
					}
				}
			} else if (!hoveredProblems.empty() && !m_linking) {
				ImGui::SetTooltip("%s", hoveredProblems.c_str());
			}
		}

		if (!readOnly && DrawMenus(graph, result)) { result.changed = true; }
		return result;
	}

	bool ShaderGraphView::DrawMenus(Graph& graph, Result& result) {
		bool changed = false;

		// A comment's title and colour, in a small window of its own (double-click a title, C, or the menus).
		if (m_openCommentEditor) {
			m_openCommentEditor = false;
			ImGui::OpenPopup("ShaderCommentEdit");
		}
		if (ImGui::BeginPopup("ShaderCommentEdit")) {
			Comment* comment = graph.FindComment(m_editComment);
			if (!comment) {
				ImGui::CloseCurrentPopup();
			} else {
				if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
				ImGui::SetNextItemWidth(260.0f);
				if (ImGui::InputText("Title", m_commentText, sizeof(m_commentText))) {
					comment->text = m_commentText;
					changed = true;
				}
				const bool entered = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
				if (ImGui::ColorEdit3("Colour", comment->colour.data(), ImGuiColorEditFlags_NoInputs)) { changed = true; }
				if (entered) { ImGui::CloseCurrentPopup(); }
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("ShaderCommentMenu")) {
			if (ImGui::MenuItem("Title and colour...", "double-click")) { EditComment(graph, m_comment); }
			if (ImGui::MenuItem("Delete comment", "Del")) {
				if (DeleteComments(graph, { m_comment }) != 0) { changed = true; }
				m_comment = 0;
			}
			ImGui::TextDisabled("Deleting a comment keeps the nodes in it.");
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("ShaderNodeMenu")) {
			const uint32_t output = OutputNodeOf(graph);
			const bool onlyOutput = m_selected.size() == 1 && m_selected.front() == output;
			if (ImGui::MenuItem("Copy", "Ctrl+C", false, !onlyOutput)) { m_clipboard = Copy(graph, m_selected); }
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !onlyOutput)) {
				Duplicate(graph);
				changed = true;
			}
			if (ImGui::MenuItem("Delete", "Del", false, !onlyOutput)) {
				if (DeleteNodes(graph, m_selected) != 0) { changed = true; }
				ClearSelection();
			}
			if (ImGui::MenuItem("Comment around them", "C") && CommentAroundSelection(graph)) { changed = true; }
			// A sub-graph node: point it at another sub-graph (or find its own again, after its GUID changed).
			if (const Node* only = m_selected.size() == 1 ? graph.FindNode(m_selected.front()) : nullptr; only && only->type == kSubgraphNode) {
				const uint32_t id = only->id;
				const auto current = only->settings.find(kSubgraphSetting);
				if (ImGui::BeginMenu("Use sub-graph")) {
					const std::vector<const SubgraphEntry*> subgraphs = ListSubgraphs();
					for (const SubgraphEntry* entry : subgraphs) {
						const bool chosen = current != only->settings.end() && FindSubgraph(current->second) == entry;
						if (ImGui::MenuItem(SubgraphTitle(*entry).c_str(), nullptr, chosen, entry->error.empty()) && !chosen && SetSubgraph(graph, id, entry->guid)) { changed = true; }
					}
					if (subgraphs.empty()) { ImGui::TextDisabled("No sub-graphs in Assets."); }
					ImGui::EndMenu();
				}
			}
			if (onlyOutput) { ImGui::TextDisabled("A graph always has its Output node."); }
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("ShaderLinkMenu")) {
			if (ImGui::MenuItem("Add reroute point", "double-click") && m_hasLink) {
				const uint32_t reroute = InsertReroute(graph, m_link.toNode, m_link.toPin, m_menuPosition.x - kRerouteWidth * 0.5f,
				                                       m_menuPosition.y - kRerouteHeight * 0.5f, result.refused);
				if (reroute != 0) {
					changed = true;
					Select(reroute);
				}
			}
			if (ImGui::MenuItem("Delete link", "Del") && m_hasLink) {
				if (DeleteLinkInto(graph, m_link.toNode, m_link.toPin)) { changed = true; }
				m_hasLink = false;
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("ShaderAddMenu")) {
			if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputTextWithHint("##filter", "Search nodes...", m_filter, sizeof(m_filter));
			// Enter in the box takes the first match.
			const bool entered = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
			const std::string filter = Lower(m_filter);
			const bool hasOutput = OutputNodeOf(graph) != 0;

			uint32_t added = 0;
			const auto add = [&](std::string_view type, std::string_view parameter) {
				added = AddNode(graph, type, m_menuPosition.x, m_menuPosition.y, parameter);
			};
			// Whether a row of the table is offered: not a second Output; Parameter nodes are
			// offered per parameter instead.
			const auto offered = [&](const NodeType& type) {
				const std::string_view name = type.name;
				return name != kParameterNode && !(name == kOutputNode && hasOutput);
			};

			if (filter.empty()) {
				if (!m_menuLinks && ImGui::MenuItem("Comment")) {
					const uint32_t comment = graph.AddComment(m_menuPosition.x, m_menuPosition.y, 300.0f, 200.0f, "Comment");
					ClearSelection();
					m_comment = comment;
					EditComment(graph, comment);
					changed = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SetItemTooltip("A box with a title, for notes. Select nodes and press C to put one around them.");
				if (!m_menuLinks && !m_clipboard.Empty() && ImGui::MenuItem("Paste", "Ctrl+V")) {
					m_selected = Paste(graph, m_clipboard, m_menuPosition.x, m_menuPosition.y);
					m_hasLink = false;
					changed = true;
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::BeginMenu("Parameter")) {
					for (const Parameter& parameter : graph.parameters) {
						if (ImGui::MenuItem(parameter.name.c_str(), ParameterTypeName(parameter.type))) { add(kParameterNode, parameter.name); }
					}
					if (graph.parameters.empty()) { ImGui::TextDisabled("No parameters yet - add one in the list on the left."); }
					ImGui::EndMenu();
				}
				// Every sub-graph in Assets (.shadersubgraph): one node each, its pins its inputs and outputs.
				if (ImGui::BeginMenu("Sub-graph")) {
					const std::vector<const SubgraphEntry*> subgraphs = ListSubgraphs();
					for (const SubgraphEntry* entry : subgraphs) {
						if (ImGui::MenuItem(SubgraphTitle(*entry).c_str(), nullptr, false, entry->error.empty())) { add(kSubgraphNode, entry->guid); }
						if (!entry->error.empty()) { ImGui::SetItemTooltip("Cannot be used: %s", entry->error.c_str()); }
					}
					if (subgraphs.empty()) { ImGui::TextDisabled("No sub-graphs yet - a .shadersubgraph file in Assets."); }
					ImGui::EndMenu();
				}
				// One submenu per category, in the table's order.
				std::vector<std::string_view> categories;
				for (const NodeType& type : NodeTypes()) {
					if (offered(type) && std::find(categories.begin(), categories.end(), std::string_view(type.category)) == categories.end()) {
						categories.emplace_back(type.category);
					}
				}
				for (const std::string_view category : categories) {
					if (!ImGui::BeginMenu(std::string(category).c_str())) { continue; }
					for (const NodeType& type : NodeTypes()) {
						if (!offered(type) || category != type.category) { continue; }
						if (ImGui::MenuItem(Title(type.name).c_str())) { add(type.name, {}); }
						ImGui::SetItemTooltip("%s", type.summary);
					}
					ImGui::EndMenu();
				}
			} else {
				// Everything that matches, in one list; Enter takes the first.
				bool first = true, any = false;
				for (const NodeType& type : NodeTypes()) {
					if (!offered(type) || (Lower(type.name).find(filter) == std::string::npos && Lower(type.category).find(filter) == std::string::npos)) { continue; }
					any = true;
					if (ImGui::Selectable(Title(type.name).c_str(), first) || (first && entered)) { add(type.name, {}); }
					ImGui::SetItemTooltip("%s", type.summary);
					first = false;
				}
				for (const SubgraphEntry* entry : ListSubgraphs()) {
					if (!entry->error.empty() || (Lower(entry->label).find(filter) == std::string::npos && std::string_view("sub-graph").find(filter) == std::string_view::npos)) { continue; }
					any = true;
					if (ImGui::Selectable(("Sub-graph: " + SubgraphTitle(*entry)).c_str(), first) || (first && entered)) { add(kSubgraphNode, entry->guid); }
					first = false;
				}
				for (const Parameter& parameter : graph.parameters) {
					if (Lower(parameter.name).find(filter) == std::string::npos && std::string_view("parameter").find(filter) == std::string_view::npos) { continue; }
					any = true;
					if (ImGui::Selectable(("Parameter: " + parameter.name).c_str(), first) || (first && entered)) { add(kParameterNode, parameter.name); }
					first = false;
				}
				if (!any) { ImGui::TextDisabled("No node is called that."); }
			}

			if (added != 0) {
				changed = true;
				if (m_menuLinks && !LinkNewNode(graph, added)) { result.refused = "The new node has no pin that takes that link, so it is not linked."; }
				Select(added);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		return changed;
	}

}

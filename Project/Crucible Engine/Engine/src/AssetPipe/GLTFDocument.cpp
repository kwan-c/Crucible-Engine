#include "GLTFDocument.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <rapidjson/error/en.h>

namespace Crucible::GLTFDetail {
    namespace {
        bool Fail(std::string &error, std::string message) {
            error = std::move(message);
            return false;
        }
        uint32_t LE(const unsigned char *bytes, size_t width = 4) {
            uint32_t value = 0;
            for (size_t i = 0; i < width; ++i)
                value |= uint32_t(bytes[i]) << (8 * i);
            return value;
        }
        size_t Width(Component type) {
            switch (type) {
            case Component::Byte:
            case Component::UByte:
                return 1;
            case Component::Short:
            case Component::UShort:
                return 2;
            default:
                return 4;
            }
        }
        bool IsUnsigned(Component type) {
            return type == Component::UByte || type == Component::UShort || type == Component::UInt;
        }
        bool Size(const rapidjson::Value &object, const char *name, size_t &result, std::string &error,
                  bool optional = false) {
            if (!object.IsObject())
                return Fail(error, "Expected an object");
            auto field = object.FindMember(name);
            if (field == object.MemberEnd()) {
                result = 0;
                return optional || Fail(error, std::string("Missing ") + name);
            }
            if (!field->value.IsUint64() || field->value.GetUint64() > std::numeric_limits<size_t>::max())
                return Fail(error, std::string(name) + " must be a nonnegative integer within size_t range");
            result = static_cast<size_t>(field->value.GetUint64());
            return true;
        }
        const rapidjson::Value *Entry(const rapidjson::Value &root, const char *name, size_t index,
                                      std::string &error) {
            auto field = root.FindMember(name);
            if (field == root.MemberEnd() || !field->value.IsArray()) {
                Fail(error, std::string("Missing or invalid ") + name + " array");
                return nullptr;
            }
            if (index >= field->value.Size()) {
                Fail(error, std::string(name) + " index out of range: " + std::to_string(index));
                return nullptr;
            }
            const auto &entry = field->value[static_cast<rapidjson::SizeType>(index)];
            if (!entry.IsObject()) {
                Fail(error, std::string(name) + " entry must be an object");
                return nullptr;
            }
            return &entry;
        }
    } // namespace

    bool Document::Parse(std::span<const unsigned char> bytes, std::string &error) {
        error.clear();
        m_ready = false;
        m_json.SetNull();
        m_binary.clear();
        m_bufferLength = 0;
        if (bytes.size() < 12)
            return Fail(error, "Incomplete GLB header");
        if (LE(bytes.data()) != 0x46546C67u)
            return Fail(error, "Invalid GLB magic");
        if (LE(bytes.data() + 4) != 2)
            return Fail(error, "Unsupported GLB version: " + std::to_string(LE(bytes.data() + 4)));
        if (LE(bytes.data() + 8) != bytes.size())
            return Fail(error, "GLB declared length does not match file size");
        std::span<const unsigned char> json;
        bool hasJSON = false, hasBIN = false;
        size_t cursor = 12, ordinal = 0;
        while (cursor < bytes.size()) {
            if (bytes.size() - cursor < 8)
                return Fail(error, "Incomplete GLB chunk header");
            uint32_t length = LE(bytes.data() + cursor), tag = LE(bytes.data() + cursor + 4);
            cursor += 8;
            if (length > bytes.size() - cursor)
                return Fail(error, "GLB chunk exceeds file bounds");
            if (length % 4)
                return Fail(error, "GLB chunk length must be aligned to four bytes");
            if (ordinal == 0 && tag != 0x4E4F534Au)
                return Fail(error, "JSON chunk must be first");
            if (tag == 0x4E4F534Au) {
                if (hasJSON)
                    return Fail(error, "Duplicate JSON chunk");
                hasJSON = true;
                json = bytes.subspan(cursor, length);
            } else if (tag == 0x004E4942u) {
                if (hasBIN)
                    return Fail(error, "Duplicate BIN chunk");
                if (ordinal != 1)
                    return Fail(error, "BIN chunk must be second");
                hasBIN = true;
                m_binary.assign(bytes.begin() + cursor, bytes.begin() + cursor + length);
            } // Well-formed unknown chunks are ignored per GLB 2.0.
            cursor += length;
            ++ordinal;
        }
        if (!hasJSON || json.empty())
            return Fail(error, "Missing JSON chunk");
        if (std::find(json.begin(), json.end(), 0) != json.end())
            return Fail(error, "JSON chunk contains a NUL byte");
        // Bound DOM destruction depth too, not only the parser's own stack. Braces
        // inside quoted/escaped strings do not count toward structural nesting.
        size_t depth = 0;
        bool quoted = false, escaped = false;
        for (unsigned char c : json) {
            if (quoted) {
                if (escaped)
                    escaped = false;
                else if (c == '\\')
                    escaped = true;
                else if (c == '"')
                    quoted = false;
            } else if (c == '"')
                quoted = true;
            else if (c == '{' || c == '[') {
                if (++depth > 128)
                    return Fail(error, "JSON nesting exceeds supported limit (128)");
            } else if ((c == '}' || c == ']') && depth)
                --depth;
        }
        m_json.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(
            reinterpret_cast<const char *>(json.data()), json.size());
        if (m_json.HasParseError())
            return Fail(error, std::string("Malformed JSON: ") +
                                   rapidjson::GetParseError_En(m_json.GetParseError()));
        if (!m_json.IsObject())
            return Fail(error, "glTF root must be an object");
        auto asset = m_json.FindMember("asset");
        if (asset == m_json.MemberEnd() || !asset->value.IsObject())
            return Fail(error, "Missing glTF asset metadata");
        auto version = asset->value.FindMember("version");
        if (version == asset->value.MemberEnd() || !version->value.IsString() ||
            std::string_view(version->value.GetString(), version->value.GetStringLength()) != "2.0")
            return Fail(error, "Unsupported asset.version; expected 2.0");
        auto minimum = asset->value.FindMember("minVersion");
        if (minimum != asset->value.MemberEnd() &&
            (!minimum->value.IsString() ||
             std::string_view(minimum->value.GetString(), minimum->value.GetStringLength()) != "2.0"))
            return Fail(error, "Unsupported asset.minVersion");
        auto required = m_json.FindMember("extensionsRequired");
        if (required != m_json.MemberEnd()) {
            if (!required->value.IsArray())
                return Fail(error, "extensionsRequired must be an array");
            if (!required->value.Empty()) {
                const auto &extension = required->value[0];
                return Fail(error, "Required glTF extension is unsupported: " +
                                       (extension.IsString()
                                            ? std::string(extension.GetString(), extension.GetStringLength())
                                            : "<invalid name>"));
            }
        }
        auto buffers = m_json.FindMember("buffers");
        if (buffers != m_json.MemberEnd()) {
            if (!buffers->value.IsArray() || buffers->value.Size() != 1 || !buffers->value[0].IsObject())
                return Fail(error, "Only one embedded buffer is supported");
            const auto &buffer = buffers->value[0];
            if (buffer.HasMember("uri"))
                return Fail(error, "External/data-URI buffers are unsupported; use an embedded GLB buffer");
            if (!Size(buffer, "byteLength", m_bufferLength, error))
                return false;
            if (m_bufferLength == 0)
                return Fail(error, "buffer byteLength must be positive");
            if (!hasBIN)
                return Fail(error, "Missing BIN chunk for embedded buffer");
            if (m_bufferLength > m_binary.size())
                return Fail(error, "buffer byteLength exceeds BIN chunk");
            if (m_binary.size() - m_bufferLength > 3)
                return Fail(error, "BIN exceeds buffer byteLength plus allowed padding");
        } else if (!m_binary.empty())
            return Fail(error, "BIN data has no declared buffer");
        m_ready = true;
        return true;
    }

    bool Document::BufferView(size_t index, std::span<const unsigned char> &bytes, std::string &error) const {
        error.clear();
        bytes = {};
        if (!m_ready)
            return Fail(error, "No parsed GLB document");
        const auto *view = Entry(m_json, "bufferViews", index, error);
        if (!view)
            return false;
        size_t buffer = 0, offset = 0, length = 0;
        if (!Size(*view, "buffer", buffer, error) || !Size(*view, "byteOffset", offset, error, true) ||
            !Size(*view, "byteLength", length, error))
            return false;
        if (buffer != 0 || !m_json.HasMember("buffers"))
            return Fail(error, "bufferView references an invalid embedded buffer");
        if (view->HasMember("byteStride")) {
            size_t stride = 0;
            if (!Size(*view, "byteStride", stride, error))
                return false;
            if (stride < 4 || stride > 252 || stride % 4)
                return Fail(error, "byteStride must be a multiple of four in [4,252]");
        }
        if (view->HasMember("target")) {
            size_t target = 0;
            if (!Size(*view, "target", target, error))
                return false;
            if (target != 34962 && target != 34963)
                return Fail(error, "Invalid bufferView target");
        }
        if (!length)
            return Fail(error, "bufferView byteLength must be positive");
        if (offset > m_bufferLength || length > m_bufferLength - offset)
            return Fail(error, "bufferView exceeds embedded buffer bounds");
        bytes = std::span<const unsigned char>(m_binary).subspan(offset, length);
        return true;
    }

    bool Document::GetAccessor(size_t index, Accessor &out, std::string &error) const {
        out = {};
        error.clear();
        if (!m_ready)
            return Fail(error, "No parsed GLB document");
        const auto *a = Entry(m_json, "accessors", index, error);
        if (!a)
            return false;
        if (a->HasMember("sparse"))
            return Fail(error, "Sparse accessors are unsupported");
        if (!a->HasMember("bufferView"))
            return Fail(error, "bufferView-less accessors are unsupported");
        size_t viewIndex = 0, format = 0, count = 0, offset = 0;
        if (!Size(*a, "bufferView", viewIndex, error) || !Size(*a, "componentType", format, error) ||
            !Size(*a, "count", count, error) || !Size(*a, "byteOffset", offset, error, true))
            return false;
        if (format != 5120 && format != 5121 && format != 5122 && format != 5123 && format != 5125 &&
            format != 5126)
            return Fail(error, "Unsupported accessor componentType: " + std::to_string(format));
        Accessor v;
        v.m_format = static_cast<Component>(format);
        auto type = a->FindMember("type");
        if (type == a->MemberEnd() || !type->value.IsString())
            return Fail(error, "Accessor type must be a string");
        std::string_view shape(type->value.GetString(), type->value.GetStringLength());
        if (shape == "SCALAR") {
            v.m_shape = Shape::Scalar;
            v.m_components = 1;
        } else if (shape == "VEC2") {
            v.m_shape = Shape::Vec2;
            v.m_components = 2;
        } else if (shape == "VEC3") {
            v.m_shape = Shape::Vec3;
            v.m_components = 3;
        } else if (shape == "VEC4") {
            v.m_shape = Shape::Vec4;
            v.m_components = 4;
        } else if (shape == "MAT4") {
            v.m_shape = Shape::Mat4;
            v.m_components = 16;
        } else
            return Fail(error, "Unsupported accessor type: " + std::string(shape));
        if (v.m_shape == Shape::Mat4 && v.m_format != Component::Float)
            return Fail(error, "Only FLOAT MAT4 is supported");
        auto normalized = a->FindMember("normalized");
        if (normalized != a->MemberEnd()) {
            if (!normalized->value.IsBool())
                return Fail(error, "normalized must be a boolean");
            v.m_normalized = normalized->value.GetBool();
        }
        if (v.m_normalized && (v.m_format == Component::Float || v.m_format == Component::UInt))
            return Fail(error, "FLOAT/UNSIGNED_INT cannot be normalized");
        if (!count)
            return Fail(error, "Accessor count must be positive");
        std::span<const unsigned char> data;
        if (!BufferView(viewIndex, data, error))
            return false;
        const auto &view = m_json["bufferViews"][static_cast<rapidjson::SizeType>(viewIndex)];
        size_t viewOffset = 0;
        if (!Size(view, "byteOffset", viewOffset, error, true))
            return false;
        const auto width = Width(v.m_format), element = width * v.m_components;
        v.m_stride = element;
        v.m_explicitStride = view.HasMember("byteStride");
        if (v.m_explicitStride) {
            if (!Size(view, "byteStride", v.m_stride, error))
                return false;
            if (v.m_stride < 4 || v.m_stride > 252 || v.m_stride % 4)
                return Fail(error, "byteStride must be a multiple of four in [4,252]");
        }
        if (v.m_stride < element || v.m_stride % width)
            return Fail(error, "Accessor stride is smaller than its element or misaligned");
        // Division precedes multiplication; attacker-controlled count cannot overflow.
        if (offset > data.size() || element > data.size() - offset ||
            count - 1 > (data.size() - offset - element) / v.m_stride)
            return Fail(error, "Accessor exceeds bufferView bounds");
        v.m_offset = viewOffset + offset;
        v.m_relativeOffset = offset;
        if (offset % width || v.m_offset % width)
            return Fail(error, "Accessor offset is not component-aligned");
        v.m_count = count;
        v.m_data = data.subspan(offset, (count - 1) * v.m_stride + element);
        out = v;
        return true;
    }

    bool Accessor::Floats(std::vector<float> &out, std::string &error) const {
        out.clear();
        error.clear();
        if (!Valid())
            return Fail(error, "Invalid accessor view");
        if (m_count > out.max_size() / m_components)
            return Fail(error, "Accessor output exceeds vector capacity");
        out.resize(m_count * m_components);
        const auto width = Width(m_format);
        for (size_t i = 0; i < m_count; ++i)
            for (size_t c = 0; c < m_components; ++c) {
                uint32_t bits = LE(m_data.data() + i * m_stride + c * width, width);
                float value = 0;
                switch (m_format) {
                case Component::Float:
                    value = std::bit_cast<float>(bits);
                    break;
                case Component::UByte:
                    value = m_normalized ? float(bits) / 255.0f : float(bits);
                    break;
                case Component::UShort:
                    value = m_normalized ? float(bits) / 65535.0f : float(bits);
                    break;
                case Component::UInt:
                    value = float(bits);
                    break;
                case Component::Byte: {
                    auto integer = std::bit_cast<int8_t>(static_cast<uint8_t>(bits));
                    value = m_normalized ? std::max(-1.0f, float(integer) / 127.0f) : float(integer);
                    break;
                }
                case Component::Short: {
                    auto integer = std::bit_cast<int16_t>(static_cast<uint16_t>(bits));
                    value = m_normalized ? std::max(-1.0f, float(integer) / 32767.0f) : float(integer);
                    break;
                }
                }
                if (!std::isfinite(value)) {
                    out.clear();
                    return Fail(error, "Accessor contains a non-finite float");
                }
                out[i * m_components + c] = value;
            }
        return true;
    }

    bool Accessor::Unsigned(std::vector<uint32_t> &out, std::string &error) const {
        out.clear();
        error.clear();
        if (!Valid())
            return Fail(error, "Invalid accessor view");
        if (!IsUnsigned(m_format) || m_normalized)
            return Fail(error, "Expected non-normalized unsigned integer accessor");
        if (m_count > out.max_size() / m_components)
            return Fail(error, "Accessor output exceeds vector capacity");
        out.resize(m_count * m_components);
        const auto width = Width(m_format);
        for (size_t i = 0; i < m_count; ++i)
            for (size_t c = 0; c < m_components; ++c)
                out[i * m_components + c] = LE(m_data.data() + i * m_stride + c * width, width);
        return true;
    }
} // namespace Crucible::GLTFDetail

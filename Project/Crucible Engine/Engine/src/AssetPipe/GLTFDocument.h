#pragma once

// Shared container/accessor layer used by GLTFLoader. Views borrow this document;
// bulk reads return owned vectors. No engine/GPU dependency.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <rapidjson/document.h>

namespace Crucible::GLTFDetail {
    enum class Component : uint32_t {
        Byte = 5120,
        UByte = 5121,
        Short = 5122,
        UShort = 5123,
        UInt = 5125,
        Float = 5126
    };
    enum class Shape {
        Scalar,
        Vec2,
        Vec3,
        Vec4,
        Mat4
    };

    class Accessor {
      public:
        bool Valid() const { return !m_data.empty(); }
        size_t Count() const { return m_count; }
        size_t Components() const { return m_components; }
        size_t Stride() const { return m_stride; }
        size_t Offset() const { return m_offset; }
        size_t RelativeOffset() const { return m_relativeOffset; }
        Component Format() const { return m_format; }
        Shape Type() const { return m_shape; }
        bool Normalized() const { return m_normalized; }
        bool ExplicitStride() const { return m_explicitStride; }
        bool Floats(std::vector<float> &out, std::string &error) const;
        bool Unsigned(std::vector<uint32_t> &out, std::string &error) const;

      private:
        friend class Document;
        std::span<const unsigned char> m_data;
        size_t m_count = 0, m_components = 0, m_stride = 0, m_offset = 0, m_relativeOffset = 0;
        Component m_format = Component::Float;
        Shape m_shape = Shape::Scalar;
        bool m_normalized = false, m_explicitStride = false;
    };

    class Document {
      public:
        bool Parse(std::span<const unsigned char> bytes, std::string &error);
        const rapidjson::Document &Json() const { return m_json; }
        bool BufferView(size_t index, std::span<const unsigned char> &bytes, std::string &error) const;
        bool GetAccessor(size_t index, Accessor &accessor, std::string &error) const;

      private:
        rapidjson::Document m_json;
        std::vector<unsigned char> m_binary;
        size_t m_bufferLength = 0;
        bool m_ready = false;
    };
} // namespace Crucible::GLTFDetail

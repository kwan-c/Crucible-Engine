#pragma once
#include "Animation/AnimatorController.h"
#include <span>

namespace Crucible {
    inline constexpr size_t MaxAnimatorControllerBytes = 4 * 1024 * 1024;
    struct AnimatorControllerResult {
        bool ok = false;
        std::string error;
        AnimatorController controller; // empty on failure
    };
    // Engine-owned versioned JSON, like .mat; no source model parsing involved.
    ENGINE_API AnimatorControllerResult DecodeAnimatorController(std::span<const unsigned char> bytes);
    // Validates before writing; throws std::runtime_error for invalid definitions.
    ENGINE_API std::string EncodeAnimatorController(const AnimatorController&);
    // Definition equality also supports incomplete editor drafts (including NaN
    // placeholders); editor-only metadata is deliberately not part of this type.
    ENGINE_API bool SameAnimatorControllerDefinition(const AnimatorController&, const AnimatorController&);
}

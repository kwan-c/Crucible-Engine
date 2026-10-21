#include "Scripting/ScriptRuntime.h"
#include "Diagnostics/Logger.h"
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <utility>

#if CRUCIBLE_WITH_MONO
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/attrdefs.h>
#include <mono/metadata/blob.h>
#include <mono/metadata/class.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/loader.h>
#include <mono/metadata/object.h>
#include <mono/metadata/row-indexes.h>
#include <mono/metadata/reflection.h>
#include <mono/metadata/tokentype.h>
#include <mono/utils/mono-logger.h>
#include <mono/utils/mono-publib.h>
#endif

namespace Crucible {
    namespace {
#if CRUCIBLE_WITH_MONO
        enum class ProcessRuntime { NeverStarted, Active, Finished };
        ProcessRuntime processRuntime = ProcessRuntime::NeverStarted;
        std::mutex runtimeGate;
        std::thread::id runtimeOwnerThread;
        thread_local unsigned managedCallDepth = 0;

        // Managed code can call native adapters from constructors, reflection,
        // exception formatting and finalizers as well as lifecycle callbacks.
        struct ManagedCallScope {
            ManagedCallScope() { ++managedCallDepth; }
            ~ManagedCallScope() { --managedCallDepth; }
            ManagedCallScope(const ManagedCallScope&) = delete;
            ManagedCallScope& operator=(const ManagedCallScope&) = delete;
        };

        void CleanupDomain(MonoDomain* domain) {
            ManagedCallScope scope;
            mono_jit_cleanup(domain);
        }

        std::filesystem::path ExecutableDirectory() {
            std::vector<wchar_t> buffer(512);
            for (;;) {
                const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (!size) throw std::runtime_error("Cannot locate the executable directory");
                if (size < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), size)).parent_path();
                if (buffer.size() >= 32768) throw std::runtime_error("Executable path is too long");
                buffer.resize(buffer.size() * 2);
            }
        }

        std::string Utf8(const std::filesystem::path& path) {
            const auto value = path.u8string();
            return std::string(reinterpret_cast<const char*>(value.data()), value.size());
        }

        void MonoLog(const char* domain, const char* level, const char* message, mono_bool fatal, void*) {
            // Mono may log from its GC threads. Never allow a C++ exception across this C callback.
            try {
                const std::string_view severity = level ? level : "";
                if (fatal || severity == "error" || severity == "critical")
                    CRU_ERROR("[Mono/{}] {}", domain ? domain : "runtime", message ? message : "");
                else if (severity == "warning")
                    CRU_WARN("[Mono/{}] {}", domain ? domain : "runtime", message ? message : "");
                else CRU_INFO("[Mono/{}] {}", domain ? domain : "runtime", message ? message : "");
            } catch (...) {}
        }

        // Temporary pins protect native buffers/pointers during argument conversion
        // and managed calls. Persistent objects use ordinary strong, movable handles.
        struct GcRoot {
            uint32_t handle = 0;
            explicit GcRoot(MonoObject* object = nullptr, bool pinned = true)
                : handle(object ? mono_gchandle_new(object, pinned) : 0) {}
            ~GcRoot() { if (handle) mono_gchandle_free(handle); }
            GcRoot(const GcRoot&) = delete;
            GcRoot& operator=(const GcRoot&) = delete;
            GcRoot(GcRoot&& other) noexcept : handle(std::exchange(other.handle, 0)) {}
            GcRoot& operator=(GcRoot&& other) noexcept {
                if (this != &other) { if (handle) mono_gchandle_free(handle); handle = std::exchange(other.handle, 0); }
                return *this;
            }
            MonoObject* Get() const { return handle ? mono_gchandle_get_target(handle) : nullptr; }
        };

        bool ToUtf16(const std::string& value, std::wstring& wide) {
            wide.clear();
            if (value.empty()) return true;
            if (value.size() > size_t(std::numeric_limits<int>::max())) return false;
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
            if (!count) return false;
            wide.resize(count);
            return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), wide.data(), count) == count;
        }

        bool FromManagedString(MonoString* value, std::string& text) {
            text.clear();
            const int length = mono_string_length(value);
            if (!length) return true;
            const auto* chars = reinterpret_cast<const wchar_t*>(mono_string_chars(value));
            const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, nullptr, 0, nullptr, nullptr);
            if (!count) return false;
            text.resize(count);
            return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, text.data(), count, nullptr, nullptr) == count;
        }

        // C ABI boundary: never let native exceptions cross into managed code.
        void ManagedLog(int32_t level, MonoString* message) noexcept {
            try {
                GcRoot root(reinterpret_cast<MonoObject*>(message));
                std::string text;
                if (root.Get() && !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), text))
                    text = "<invalid UTF-16 log message>";
                if (level == 2) CRU_USER_ERROR("[Script] {}", text);
                else if (level == 1) CRU_USER_WARN("[Script] {}", text);
                else CRU_USER_INFO("[Script] {}", text);
            } catch (...) {}
        }

        std::string ManagedException(MonoObject* exception) {
            ManagedCallScope scope;
            GcRoot root(exception);
            auto* type = mono_object_get_class(root.Get());
            const std::string space = mono_class_get_namespace(type);
            const auto name = (space.empty() ? "" : space + ".") + mono_class_get_name(type);
            MonoObject* formattingException = nullptr;
            auto* message = mono_object_to_string(root.Get(), &formattingException);
            if (formattingException || !message) return name + " (exception formatting unavailable)";
            GcRoot messageRoot(reinterpret_cast<MonoObject*>(message));
            std::string text;
            if (!FromManagedString(reinterpret_cast<MonoString*>(messageRoot.Get()), text)) return name + " (invalid UTF-16 diagnostic)";
            return text.starts_with(name) ? text : name + ": " + text;
        }

        // User metadata goes through checked managed reflection first. Several
        // legacy Mono native class/signature accessors assert on unresolved types.
        bool Reflect(MonoObject* target, const char* name, int count, void** arguments,
                     GcRoot& result, std::string& error) {
            GcRoot owner(target);
            MonoMethod* method = nullptr;
            for (auto* type = mono_object_get_class(owner.Get()); type && !method; type = mono_class_get_parent(type))
                method = mono_class_get_method_from_name(type, name, count);
            if (!method) { error = std::string("Framework reflection method is unavailable: ") + name; return false; }
            ManagedCallScope scope;
            MonoObject* exception = nullptr;
            auto* value = mono_runtime_invoke(method, owner.Get(), arguments, &exception);
            if (exception) { error = ManagedException(exception); return false; }
            result = GcRoot(value);
            return true;
        }

        bool ValidateMembers(MonoObject* reflectedType, std::string& error) {
            GcRoot interfaces;
            if (!Reflect(reflectedType, "GetInterfaces", 0, nullptr, interfaces, error)) return false;
            for (const char* group : {"GetMethods", "GetConstructors"}) {
                GcRoot members;
                if (!Reflect(reflectedType, group, 0, nullptr, members, error) || !members.Get()) return false;
                auto* array = reinterpret_cast<MonoArray*>(members.Get());
                for (uintptr_t i = 0; i < mono_array_length(array); ++i) {
                    GcRoot member(mono_array_get(array, MonoObject*, i));
                    GcRoot parameters;
                    if (!Reflect(member.Get(), "GetParameters", 0, nullptr, parameters, error) || !parameters.Get()) return false;
                    auto* parameterArray = reinterpret_cast<MonoArray*>(parameters.Get());
                    for (uintptr_t p = 0; p < mono_array_length(parameterArray); ++p) {
                        GcRoot parameter(mono_array_get(parameterArray, MonoObject*, p));
                        GcRoot parameterType;
                        if (!Reflect(parameter.Get(), "get_ParameterType", 0, nullptr, parameterType, error)) return false;
                    }
                    if (std::strcmp(group, "GetMethods") == 0) {
                        GcRoot returnType;
                        if (!Reflect(member.Get(), "get_ReturnType", 0, nullptr, returnType, error)) return false;
                    }
                }
            }
            // Resolve public field signatures through checked managed reflection
            // before native metadata access (missing dependencies can otherwise abort Mono).
            GcRoot fields;
            if (!Reflect(reflectedType, "GetFields", 0, nullptr, fields, error) || !fields.Get()) return false;
            auto* fieldArray = reinterpret_cast<MonoArray*>(fields.Get());
            for (uintptr_t i = 0; i < mono_array_length(fieldArray); ++i) {
                GcRoot field(mono_array_get(fieldArray, MonoObject*, i)), fieldType;
                if (!Reflect(field.Get(), "get_FieldType", 0, nullptr, fieldType, error)) return false;
            }
            return true;
        }

        bool HasGenericParameters(MonoImage* image, uint32_t token, bool method) {
            // ECMA TypeOrMethodDef coded index: low bit selects method, remaining bits are RID.
            const uint32_t owner = ((token & 0x00ffffffu) << 1) | (method ? 1u : 0u);
            const auto* table = mono_image_get_table_info(image, MONO_TABLE_GENERICPARAM);
            for (int row = 0; row < mono_table_info_get_rows(table); ++row)
                if (mono_metadata_decode_row_col(table, row, MONO_GENERICPARAM_OWNER) == owner) return true;
            return false;
        }

        bool SupportedClass(MonoClass* type) {
            const auto flags = mono_class_get_flags(type);
            return (flags & MONO_TYPE_ATTR_VISIBILITY_MASK) == MONO_TYPE_ATTR_PUBLIC &&
                !(flags & MONO_TYPE_ATTR_INTERFACE) && !mono_class_get_nesting_type(type) &&
                mono_type_get_type(mono_class_get_type(type)) == MONO_TYPE_CLASS &&
                !mono_class_is_valuetype(type) && !HasGenericParameters(mono_class_get_image(type), mono_class_get_type_token(type), false);
        }

        MonoMethod* DefaultConstructor(MonoClass* type) {
            void* iterator = nullptr;
            while (auto* method = mono_class_get_methods(type, &iterator)) {
                const auto flags = mono_method_get_flags(method, nullptr);
                if (std::strcmp(mono_method_get_name(method), ".ctor") == 0 &&
                    (flags & MONO_METHOD_ATTR_ACCESS_MASK) == MONO_METHOD_ATTR_PUBLIC &&
                    !(flags & MONO_METHOD_ATTR_STATIC) && mono_signature_get_param_count(mono_method_signature(method)) == 0) return method;
            }
            return nullptr;
        }

        enum class ValueKind { Unsupported, Void, Boolean, Int32, Single, Double, String };
        ValueKind ManagedKind(MonoType* type) {
            if (!type || mono_type_is_byref(type)) return ValueKind::Unsupported;
            switch (mono_type_get_type(type)) {
            case MONO_TYPE_VOID: return ValueKind::Void;
            case MONO_TYPE_BOOLEAN: return ValueKind::Boolean;
            case MONO_TYPE_I4: return ValueKind::Int32;
            case MONO_TYPE_R4: return ValueKind::Single;
            case MONO_TYPE_R8: return ValueKind::Double;
            case MONO_TYPE_STRING: return ValueKind::String;
            default: return ValueKind::Unsupported;
            }
        }

        bool Matches(ValueKind kind, const ScriptValue& value) {
            switch (kind) {
            case ValueKind::Boolean: return std::holds_alternative<bool>(value);
            case ValueKind::Int32: return std::holds_alternative<int32_t>(value);
            case ValueKind::Single: return std::holds_alternative<float>(value);
            case ValueKind::Double: return std::holds_alternative<double>(value);
            case ValueKind::String: return std::holds_alternative<std::string>(value) || std::holds_alternative<std::nullptr_t>(value);
            default: return false;
            }
        }

        MonoMethod* FindMethod(MonoClass* type, const std::string& name, bool isStatic,
                               const std::vector<ScriptValue>& arguments, std::string& error) {
            if (name.empty() || name.front() == '.' || name.find('\0') != std::string::npos) {
                error = "Invalid method name (constructors use CreateObject)"; return nullptr;
            }
            for (auto* current = type; current; current = mono_class_get_parent(current)) {
                MonoMethod* found = nullptr;
                void* iterator = nullptr;
                while (auto* method = mono_class_get_methods(current, &iterator)) {
                    const auto flags = mono_method_get_flags(method, nullptr);
                    if (name != mono_method_get_name(method) ||
                        (flags & MONO_METHOD_ATTR_ACCESS_MASK) != MONO_METHOD_ATTR_PUBLIC ||
                        bool(flags & MONO_METHOD_ATTR_STATIC) != isStatic || (flags & MONO_METHOD_ATTR_ABSTRACT) ||
                        HasGenericParameters(mono_class_get_image(current), mono_method_get_token(method), true)) continue;
                    auto* signature = mono_method_signature(method);
                    if (!signature || mono_signature_get_call_conv(signature) != MONO_CALL_DEFAULT ||
                        mono_signature_get_param_count(signature) != arguments.size() ||
                        ManagedKind(mono_signature_get_return_type(signature)) == ValueKind::Unsupported) continue;
                    void* parameter = nullptr;
                    bool matches = true;
                    for (const auto& argument : arguments)
                        if (!Matches(ManagedKind(mono_signature_get_params(signature, &parameter)), argument)) { matches = false; break; }
                    if (!matches) continue;
                    if (found) { error = "Ambiguous managed method overload: " + name; return nullptr; }
                    found = method;
                }
                if (found) return found; // nearest declaration wins; object has this concrete type
            }
            error = "No public supported " + std::string(isStatic ? "static" : "instance") +
                " overload matches exact argument types: " + name;
            return nullptr;
        }

        bool CallManaged(MonoDomain* domain, MonoMethod* method, MonoObject* object,
                         const std::vector<ScriptValue>& arguments, ScriptValue& result, std::string& error) {
            ManagedCallScope scope;
            struct Argument { uint8_t boolean = 0; int32_t integer = 0; float single = 0; double real = 0; GcRoot text; };
            GcRoot target(object);
            std::vector<Argument> storage(arguments.size());
            std::vector<void*> parameters(arguments.size());
            // Root/pin every string before retaining argument pointers or entering managed code.
            for (size_t i = 0; i < arguments.size(); ++i) {
                auto& slot = storage[i];
                if (auto* text = std::get_if<std::string>(&arguments[i])) {
                    std::wstring wide;
                    if (!ToUtf16(*text, wide)) { error = "String argument is not valid UTF-8 or is too long"; return false; }
                    auto* value = mono_string_new_utf16(domain, reinterpret_cast<const mono_unichar2*>(wide.data()), int32_t(wide.size()));
                    if (!value) { error = "Mono could not allocate a string argument"; return false; }
                    slot.text = GcRoot(reinterpret_cast<MonoObject*>(value));
                }
            }
            for (size_t i = 0; i < arguments.size(); ++i) {
                auto& slot = storage[i]; const auto& value = arguments[i];
                if (auto* boolean = std::get_if<bool>(&value)) { slot.boolean = *boolean ? 1 : 0; parameters[i] = &slot.boolean; }
                else if (auto* integer = std::get_if<int32_t>(&value)) { slot.integer = *integer; parameters[i] = &slot.integer; }
                else if (auto* single = std::get_if<float>(&value)) { slot.single = *single; parameters[i] = &slot.single; }
                else if (auto* real = std::get_if<double>(&value)) { slot.real = *real; parameters[i] = &slot.real; }
                else parameters[i] = slot.text.Get(); // null String is passed as null
            }
            MonoObject* exception = nullptr;
            auto* returned = mono_runtime_invoke(method, target.Get(), parameters.data(), &exception);
            if (exception) { error = ManagedException(exception); return false; }
            GcRoot value(returned);
            const auto kind = ManagedKind(mono_signature_get_return_type(mono_method_signature(method)));
            if (kind == ValueKind::Void) { result = std::monostate{}; return true; }
            if (kind == ValueKind::String) {
                if (!value.Get()) { result = nullptr; return true; }
                std::string text;
                if (!FromManagedString(reinterpret_cast<MonoString*>(value.Get()), text)) {
                    error = "Managed return string contains invalid UTF-16"; return false;
                }
                result = std::move(text); return true;
            }
            if (!value.Get()) { error = "Managed value return was unexpectedly null"; return false; }
            void* unboxed = mono_object_unbox(value.Get());
            switch (kind) {
            case ValueKind::Boolean: result = *static_cast<uint8_t*>(unboxed) != 0; break;
            case ValueKind::Int32: result = *static_cast<int32_t*>(unboxed); break;
            case ValueKind::Single: result = *static_cast<float*>(unboxed); break;
            case ValueKind::Double: result = *static_cast<double*>(unboxed); break;
            default: error = "Unsupported managed return type"; return false;
            }
            return true;
        }

        struct InspectableField { MonoClassField* field; ScriptFieldValue value; };
        std::vector<InspectableField> InspectableFields(MonoClass* type) {
            std::vector<InspectableField> result;
            for (auto* owner = type; owner; owner = mono_class_get_parent(owner)) {
                void* iterator = nullptr;
                while (auto* field = mono_class_get_fields(owner, &iterator)) {
                    const auto flags = mono_field_get_flags(field);
                    if ((flags & MONO_FIELD_ATTR_FIELD_ACCESS_MASK) != MONO_FIELD_ATTR_PUBLIC ||
                        flags & (MONO_FIELD_ATTR_STATIC | MONO_FIELD_ATTR_INIT_ONLY | MONO_FIELD_ATTR_LITERAL | MONO_FIELD_ATTR_NOT_SERIALIZED)) continue;
                    ScriptFieldValue value;
                    switch (ManagedKind(mono_field_get_type(field))) {
                    case ValueKind::Boolean: value.type = ScriptFieldType::Boolean; break;
                    case ValueKind::Int32: value.type = ScriptFieldType::Int32; break;
                    case ValueKind::Single: value.type = ScriptFieldType::Single; break;
                    case ValueKind::Double: value.type = ScriptFieldType::Double; break;
                    case ValueKind::String: value.type = ScriptFieldType::String; break;
                    default: continue;
                    }
                    const std::string space = mono_class_get_namespace(owner);
                    value.declaringType = (space.empty() ? "" : space + ".") + mono_class_get_name(owner);
                    value.name = mono_field_get_name(field);
                    result.push_back({field, std::move(value)});
                }
            }
            return result;
        }
        bool ReadFields(MonoObject* object, MonoClass* type, std::vector<ScriptFieldValue>& fields, std::string& error) {
            GcRoot owner(object);
            std::vector<ScriptFieldValue> result;
            for (auto entry : InspectableFields(type)) {
                auto& value = entry.value;
                switch (value.type) {
                case ScriptFieldType::Boolean: {
                    uint8_t flag = 0; mono_field_get_value(owner.Get(), entry.field, &flag); value.boolean = flag != 0; break;
                }
                case ScriptFieldType::Int32: mono_field_get_value(owner.Get(), entry.field, &value.integer); break;
                case ScriptFieldType::Single: mono_field_get_value(owner.Get(), entry.field, &value.single); break;
                case ScriptFieldType::Double: mono_field_get_value(owner.Get(), entry.field, &value.real); break;
                case ScriptFieldType::String: {
                    MonoString* text = nullptr; mono_field_get_value(owner.Get(), entry.field, &text);
                    GcRoot stringRoot(reinterpret_cast<MonoObject*>(text));
                    value.stringIsNull = !text;
                    if (text && !FromManagedString(reinterpret_cast<MonoString*>(stringRoot.Get()), value.text)) {
                        error = "Public field contains invalid UTF-16: " + value.name; return false;
                    }
                    break;
                }
                }
                result.push_back(std::move(value));
            }
            fields = std::move(result); return true;
        }
        bool WriteFields(MonoDomain* domain, MonoObject* object, MonoClass* type,
                         const std::vector<ScriptFieldValue>& fields, std::string& error) {
            const auto schema = InspectableFields(type);
            std::vector<std::pair<MonoClassField*, const ScriptFieldValue*>> writes;
            // Validate the entire batch first. Obsolete/renamed/type-changed overrides
            // remain serialized but cannot write a different field after recompilation.
            for (const auto& value : fields) {
                const auto found = std::find_if(schema.begin(), schema.end(), [&](const auto& entry) { return SameScriptField(entry.value, value); });
                if (found == schema.end()) continue;
                if ((value.type == ScriptFieldType::Single && !std::isfinite(value.single)) ||
                    (value.type == ScriptFieldType::Double && !std::isfinite(value.real))) {
                    error = "Public field requires a finite number: " + value.name; return false;
                }
                if (value.type == ScriptFieldType::String && !value.stringIsNull) {
                    std::wstring wide;
                    if (!ToUtf16(value.text, wide)) { error = "Public field is not valid UTF-8: " + value.name; return false; }
                }
                writes.emplace_back(found->field, &value);
            }
            GcRoot owner(object);
            for (const auto& [field, input] : writes) {
                auto value = *input;
                switch (value.type) {
                case ScriptFieldType::Boolean: {
                    uint8_t flag = value.boolean ? 1 : 0; mono_field_set_value(owner.Get(), field, &flag); break;
                }
                case ScriptFieldType::Int32: mono_field_set_value(owner.Get(), field, &value.integer); break;
                case ScriptFieldType::Single: mono_field_set_value(owner.Get(), field, &value.single); break;
                case ScriptFieldType::Double: mono_field_set_value(owner.Get(), field, &value.real); break;
                case ScriptFieldType::String: {
                    std::wstring wide; ToUtf16(value.text, wide);
                    GcRoot text(value.stringIsNull ? nullptr : reinterpret_cast<MonoObject*>(mono_string_new_utf16(domain,
                        reinterpret_cast<const mono_unichar2*>(wide.data()), static_cast<int32_t>(wide.size()))));
                    mono_field_set_value(owner.Get(), field, text.Get()); break;
                }
                }
            }
            return true;
        }

        MonoMethod* LifecycleMethod(MonoDomain* domain, MonoClass* type, const char* name,
                                    const std::vector<ScriptValue>& arguments, std::string& error) {
            auto* method = FindMethod(type, name, false, arguments, error);
            if (!method || ManagedKind(mono_signature_get_return_type(mono_method_signature(method))) != ValueKind::Void) {
                error = std::string("Lifecycle requires public instance void ") + name +
                    (arguments.empty() ? "()" : "(float)");
                return nullptr;
            }
            // async void looks like void in a signature but can outlive this call
            // and raise exceptions outside mono_runtime_invoke's exception guard.
            auto* marker = mono_class_from_name(mono_get_corlib(), "System.Runtime.CompilerServices", "AsyncStateMachineAttribute");
            if (!marker) { error = "Framework async-method metadata is unavailable"; return nullptr; }
            GcRoot reflected(reinterpret_cast<MonoObject*>(mono_method_get_object(domain, method, type)));
            GcRoot attribute(reinterpret_cast<MonoObject*>(mono_type_get_object(domain, mono_class_get_type(marker))));
            uint8_t inherit = 0;
            void* parameters[] = {attribute.Get(), &inherit};
            GcRoot defined;
            if (!Reflect(reflected.Get(), "IsDefined", 2, parameters, defined, error) || !defined.Get()) return nullptr;
            if (*static_cast<uint8_t*>(mono_object_unbox(defined.Get()))) {
                error = std::string("Lifecycle callbacks must be synchronous; async void is unsupported: ") + name;
                return nullptr;
            }
            return method;
        }

        bool CheckManagedExecution(std::string& error) {
            // Execute a known BCL method through Mono's JIT. This verifies managed
            // execution without adding an assembly loader or gameplay API in S1.
            auto* descriptor = mono_method_desc_new("System.Math:Abs(int)", true);
            if (!descriptor) { error = "Could not create the managed verification descriptor"; return false; }
            auto* method = mono_method_desc_search_in_image(descriptor, mono_get_corlib());
            mono_method_desc_free(descriptor);
            if (!method) { error = "System.Math.Abs(Int32) is unavailable in the deployed framework"; return false; }
            int32_t input = -314159;
            void* arguments[] = {&input};
            ManagedCallScope scope;
            MonoObject* exception = nullptr;
            auto* result = mono_runtime_invoke(method, nullptr, arguments, &exception);
            if (exception) {
                auto* type = mono_object_get_class(exception);
                error = std::string("Managed verification threw ") + mono_class_get_namespace(type) + "." + mono_class_get_name(type);
                return false;
            }
            if (!result || mono_object_get_class(result) != mono_get_int32_class() ||
                *static_cast<int32_t*>(mono_object_unbox(result)) != 314159) {
                error = "Managed verification returned an unexpected result";
                return false;
            }
            CRU_INFO("[Scripting] Managed execution verified: System.Math.Abs(-314159) = 314159");
            return true;
        }
#endif
    }

    struct ScriptRuntime::Impl {
        ScriptRuntimeState state = ScriptRuntimeState::NotStarted;
        std::string version, error;
        bool verified = false;
#if CRUCIBLE_WITH_MONO
        MonoDomain* root = nullptr;
        std::thread::id ownerThread;
        struct Assembly { std::filesystem::path path; std::string name; MonoAssembly* assembly; MonoImage* image; };
        struct Object { uint32_t handle; MonoClass* type; };
        uint64_t nextAssembly = 1, nextObject = 1;
        std::map<uint64_t, Assembly> assemblies;
        std::map<uint64_t, Object> objects;
        std::set<MonoClass*> validatedClasses;
        std::map<MonoClass*, std::vector<ScriptFieldValue>> fieldDefaults;
        struct Instance {
            ScriptInstanceInfo info;
            ScriptObjectHandle object;
            MonoMethod* create = nullptr;
            MonoMethod* update = nullptr;
            MonoMethod* destroy = nullptr;
        };
        uint64_t nextInstance = 1;
        std::map<uint64_t, Instance> instances;

        Instance* GetInstance(ScriptInstanceHandle handle) {
            const auto found = instances.find(handle.value);
            if (found == instances.end()) { Fail("Unknown or stale script instance handle"); return nullptr; }
            return &found->second;
        }
        std::string Label(const Instance& instance) const {
            return (instance.info.nameSpace.empty() ? "" : instance.info.nameSpace + ".") + instance.info.name +
                " [" + std::to_string(instance.info.handle.value) + "]";
        }
        bool Callback(Instance& instance, MonoMethod* method, const char* name,
                      const std::vector<ScriptValue>& arguments, std::string& failure) {
            const auto found = objects.find(instance.object.value);
            if (found == objects.end()) { failure = Label(instance) + ": managed object was released"; return false; }
            ScriptValue result;
            auto* target = mono_gchandle_get_target(found->second.handle);
            if (!target) { failure = Label(instance) + ": managed object was released"; return false; }
            if (!CallManaged(root, method, target, arguments, result, failure)) {
                failure = Label(instance) + " " + name + ": " + failure;
                return false;
            }
            return true;
        }
        void ReleaseObject(ScriptObjectHandle& object) {
            const auto found = objects.find(object.value);
            if (found != objects.end()) { mono_gchandle_free(found->second.handle); objects.erase(found); }
            object = {};
        }
        bool Dispose(Instance& instance, std::string& failure) {
            if (!instance.object) return true; // fault cleanup already attempted OnDestroy
            bool success;
            try { success = Callback(instance, instance.destroy, "OnDestroy", {}, failure); }
            catch (...) { ReleaseObject(instance.object); throw; }
            ReleaseObject(instance.object); // release even when managed teardown throws
            return success;
        }
        bool StopInstances() {
            std::string firstError;
            for (auto it = instances.rbegin(); it != instances.rend(); ++it) {
                std::string failure;
                if (!Dispose(it->second, failure)) {
                    if (firstError.empty()) firstError = failure;
                    Fail(failure);
                }
                CRU_INFO("[Scripting] Stopped lifecycle instance {} after {} updates", Label(it->second), it->second.info.updateCount);
            }
            instances.clear();
            error = std::move(firstError);
            return error.empty();
        }
        bool Step(Instance& instance, float deltaTime) {
            if (instance.info.state != ScriptInstanceState::Running) return true;
            std::string failure;
            if (!Callback(instance, instance.update, "OnUpdate", {deltaTime}, failure)) {
                instance.info.state = ScriptInstanceState::Faulted;
                std::string cleanup;
                if (!Dispose(instance, cleanup)) failure += "\nCleanup also failed: " + cleanup;
                instance.info.lastError = failure;
                return Fail(failure);
            }
            ++instance.info.updateCount;
            instance.info.elapsedSeconds += deltaTime;
            error.clear();
            return true;
        }

        Assembly* GetAssembly(ScriptAssemblyHandle handle) {
            const auto found = assemblies.find(handle.value);
            if (found == assemblies.end()) { Fail("Unknown or stale assembly handle"); return nullptr; }
            return &found->second;
        }
        MonoClass* GetClass(Assembly& assembly, const std::string& space, const std::string& name) {
            if (name.empty() || name.find('\0') != std::string::npos || space.find('\0') != std::string::npos) {
                Fail("Invalid managed class name"); return nullptr;
            }
            const auto fullName = space.empty() ? name : space + "." + name;
            std::wstring wide;
            if (!ToUtf16(fullName, wide)) { Fail("Class name is not valid UTF-8"); return nullptr; }
            GcRoot owner(reinterpret_cast<MonoObject*>(mono_assembly_get_object(root, assembly.assembly)));
            GcRoot text(reinterpret_cast<MonoObject*>(mono_string_new_utf16(root,
                reinterpret_cast<const mono_unichar2*>(wide.data()), int32_t(wide.size()))));
            uint8_t throwOnError = 1;
            void* arguments[] = {text.Get(), &throwOnError};
            GcRoot reflectedType; std::string failure;
            if (!Reflect(owner.Get(), "GetType", 2, arguments, reflectedType, failure) || !reflectedType.Get()) {
                Fail("Resolving " + fullName + ": " + failure); return nullptr;
            }
            auto* type = mono_class_from_mono_type(mono_reflection_type_get_type(reinterpret_cast<MonoReflectionType*>(reflectedType.Get())));
            if (!type || !SupportedClass(type)) {
                Fail("Class is unsupported (requires a public top-level nongeneric reference class): " + fullName);
                return nullptr;
            }
            if (!validatedClasses.contains(type)) {
                if (!ValidateMembers(reflectedType.Get(), failure)) { Fail("Resolving members of " + fullName + ": " + failure); return nullptr; }
                validatedClasses.insert(type);
            }
            return type;
        }
        void ReleaseManaged() {
            instances.clear();
            for (const auto& [id, object] : objects) { (void)id; mono_gchandle_free(object.handle); }
            objects.clear();
            fieldDefaults.clear();
            validatedClasses.clear();
            assemblies.clear(); // metadata/images are owned by Mono until final cleanup
        }
#endif
        bool Fail(const std::string& message) {
            error = message;
            CRU_ERROR("[Scripting] {}", error);
            return false;
        }
        bool Ready() {
            if (state != ScriptRuntimeState::Running) return Fail("Managed operation requires a running Mono host");
#if CRUCIBLE_WITH_MONO
            if (ownerThread != std::this_thread::get_id()) return Fail("Managed operations must use the Mono initialization thread");
            if (managedCallDepth) return Fail("Cannot re-enter the managed host during managed execution");
#endif
            error.clear();
            return true;
        }
    };

    ScriptRuntime::ScriptRuntime() : m_impl(std::make_unique<Impl>()) {}
    ScriptRuntime::~ScriptRuntime() { Shutdown(); }
    bool ScriptRuntime::IsBuiltWithMono() noexcept { return CRUCIBLE_WITH_MONO != 0; }
    ScriptRuntimeState ScriptRuntime::State() const noexcept { return m_impl->state; }
    bool ScriptRuntime::IsRunning() const noexcept { return State() == ScriptRuntimeState::Running; }
    bool ScriptRuntime::ManagedExecutionVerified() const noexcept { return m_impl->verified; }
    const std::string& ScriptRuntime::Version() const noexcept { return m_impl->version; }
    const std::string& ScriptRuntime::LastError() const noexcept { return m_impl->error; }

    bool ScriptRuntime::Initialize(const ScriptingConfig& config) {
        auto& data = *m_impl;
#if CRUCIBLE_WITH_MONO
        // Reject before the process mutex: teardown can run managed finalizers
        // or callbacks while holding it, including callbacks into another host.
        if (managedCallDepth) return data.Fail("Cannot initialize the managed host during managed execution");
        if (runtimeOwnerThread != std::thread::id{} && runtimeOwnerThread != std::this_thread::get_id())
            return data.Fail("Mono lifecycle calls must use the initialization thread");
        std::lock_guard lock(runtimeGate);
        if (data.root) {
            if (data.ownerThread == std::this_thread::get_id()) { data.error.clear(); return true; }
            data.error = "Mono lifecycle calls must use the initialization thread";
            CRU_ERROR("[Scripting] {}", data.error);
            return false;
        }
#endif
        data.error.clear(); data.verified = false;
        if (!config.enabled) {
            data.state = ScriptRuntimeState::Disabled;
            CRU_INFO("[Scripting] Disabled by engine configuration");
            return true;
        }
#if !CRUCIBLE_WITH_MONO
        data.state = ScriptRuntimeState::Unavailable;
        CRU_INFO("[Scripting] Mono is not included in this build (CRUCIBLE_ENABLE_MONO=OFF)");
        return true;
#else
        auto fail = [&data](std::string error) {
            data.error = std::move(error); data.state = ScriptRuntimeState::Failed;
            CRU_ERROR("[Scripting] {}", data.error);
            return false;
        };
        if (processRuntime != ProcessRuntime::NeverStarted)
            return fail(processRuntime == ProcessRuntime::Active
                ? "Another Mono host is already active in this process"
                : "Mono has been shut down; restart the application before initializing it again");
        try {
            if (config.runtimeDirectory.empty()) return fail("scripting.runtimeDirectory must not be empty");
            const std::u8string directoryUtf8(config.runtimeDirectory.begin(), config.runtimeDirectory.end());
            auto directory = std::filesystem::path(directoryUtf8);
            if (directory.is_relative()) directory = ExecutableDirectory() / directory;
            directory = std::filesystem::absolute(directory).lexically_normal();
            for (const auto* file : {"lib/mono/4.5/mscorlib.dll", "etc/mono/config", "etc/mono/4.0/machine.config"}) {
                if (!std::filesystem::is_regular_file(directory / file))
                    return fail("Mono deployment is incomplete: missing " + Utf8(directory / file));
            }
            const auto library = Utf8(directory / "lib");
            const auto configuration = Utf8(directory / "etc");
            const auto configFile = Utf8(directory / "etc/mono/config");
            mono_trace_set_log_handler(MonoLog, nullptr);
            mono_set_dirs(library.c_str(), configuration.c_str());
            mono_config_parse(configFile.c_str());
            // Even a failed JIT initialization may have changed process-global state.
            processRuntime = ProcessRuntime::Finished;
            data.ownerThread = std::this_thread::get_id();
            runtimeOwnerThread = data.ownerThread;
            data.root = mono_jit_init_version("Crucible.Root", "v4.0.30319");
            if (!data.root) return fail("Mono could not initialize its CLR v4 root domain");
            processRuntime = ProcessRuntime::Active;
            mono_add_internal_call("Crucible.Log::WriteInternal", reinterpret_cast<const void*>(ManagedLog));
            if (char* version = mono_get_runtime_build_info()) {
                data.version = version; mono_free(version);
            }
            CRU_INFO("[Scripting] Mono {} initialized from {}", data.version, Utf8(directory));
            // Future subsystem-owned bindings register here before S2 loads user
            // assemblies. S1 intentionally registers no engine/internal calls.
            if (config.verifyManagedExecution && !CheckManagedExecution(data.error)) {
                CleanupDomain(data.root); data.root = nullptr;
                processRuntime = ProcessRuntime::Finished;
                return fail(data.error);
            }
            data.verified = config.verifyManagedExecution;
            data.state = ScriptRuntimeState::Running;
            return true;
        } catch (const std::exception& exception) {
            if (data.root) {
                data.ReleaseManaged();
                CleanupDomain(data.root); data.root = nullptr;
                processRuntime = ProcessRuntime::Finished;
            }
            return fail(exception.what());
        }
#endif
    }

    void ScriptRuntime::Shutdown() noexcept {
#if CRUCIBLE_WITH_MONO
        auto& data = *m_impl;
        if (!data.root) return;
        if (managedCallDepth) {
            try { CRU_ERROR("[Scripting] Shutdown refused during managed execution"); } catch (...) {}
            return;
        }
        if (data.ownerThread != std::this_thread::get_id()) {
            // Do not clean up a runtime from an unregistered/wrong native thread.
            // The owner must retry on its initialization thread before destroying the host.
            try { CRU_ERROR("[Scripting] Shutdown refused: use the Mono initialization thread"); } catch (...) {}
            return;
        }
        std::lock_guard lock(runtimeGate);
        try { data.error.clear(); data.StopInstances(); }
        catch (...) { try { CRU_ERROR("[Scripting] Native failure while disposing script instances"); } catch (...) {} }
        data.ReleaseManaged();
        CleanupDomain(data.root); data.root = nullptr;
        processRuntime = ProcessRuntime::Finished;
        data.state = ScriptRuntimeState::Stopped;
        data.verified = false;
        try { CRU_INFO("[Scripting] Mono runtime shut down"); } catch (...) {}
#endif
    }

    bool ScriptRuntime::LoadAssembly(const std::string& path, ScriptAssemblyHandle& assembly) {
        assembly = {};
        auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        try {
            if (path.empty() || path.find('\0') != std::string::npos) return data.Fail("Invalid managed assembly path");
            std::filesystem::path file(std::u8string(path.begin(), path.end()));
            if (file.is_relative()) file = ExecutableDirectory() / file;
            file = std::filesystem::weakly_canonical(file);
            if (!std::filesystem::is_regular_file(file)) return data.Fail("Managed assembly not found: " + Utf8(file));
            for (const auto& [id, loaded] : data.assemblies) {
                std::error_code comparisonError;
                if (std::filesystem::equivalent(file, loaded.path, comparisonError)) { assembly.value = id; return true; }
            }
            MonoImageOpenStatus status = MONO_IMAGE_OK;
            const auto filename = Utf8(file);
            struct ImageRef { MonoImage* image; ~ImageRef() { if (image) mono_image_close(image); } };
            ImageRef image{mono_image_open_full(filename.c_str(), &status, false)};
            if (!image.image) return data.Fail("Cannot read managed assembly " + filename + ": " + mono_image_strerror(status));
            const auto* definition = mono_image_get_table_info(image.image, MONO_TABLE_ASSEMBLY);
            if (mono_table_info_get_rows(definition) != 1) return data.Fail("Expected a managed assembly, not a native DLL or netmodule: " + filename);
            const std::string name = mono_metadata_string_heap(image.image,
                mono_metadata_decode_row_col(definition, 0, MONO_ASSEMBLY_NAME));
            const auto* references = mono_image_get_table_info(image.image, MONO_TABLE_ASSEMBLYREF);
            for (int row = 0; row < mono_table_info_get_rows(references); ++row) {
                const std::string reference = mono_metadata_string_heap(image.image,
                    mono_metadata_decode_row_col(references, row, MONO_ASSEMBLYREF_NAME));
                const uint32_t major = mono_metadata_decode_row_col(references, row, MONO_ASSEMBLYREF_MAJOR_VERSION);
                if (reference == "System.Private.CoreLib" || (reference == "System.Runtime" && major >= 5))
                    return data.Fail("Assembly targets modern .NET; rebuild for the Mono framework profile: " + filename);
            }
            for (const auto& [id, loaded] : data.assemblies) {
                (void)id;
                if (_stricmp(name.c_str(), loaded.name.c_str()) == 0)
                    return data.Fail("An assembly with this name is already loaded from another path; restart to replace it: " + name);
            }
            auto* loaded = mono_assembly_load_from_full(image.image, filename.c_str(), &status, false);
            if (!loaded) return data.Fail("Cannot load managed assembly " + filename + ": " + mono_image_strerror(status));
            auto* loadedImage = mono_assembly_get_image(loaded);
            for (const auto& [id, previous] : data.assemblies) {
                (void)id;
                if (previous.assembly == loaded) return data.Fail("Mono resolved this file to an already loaded assembly; restart to replace it");
            }
            // Reject Mono identity unification with an assembly loaded by a dependency.
            const char* actualPath = mono_image_get_filename(loadedImage);
            if (actualPath && !std::filesystem::equivalent(file, std::filesystem::path(std::u8string(actualPath, actualPath + std::strlen(actualPath)))))
                return data.Fail("Assembly identity is already bound to another file; restart to replace it: " + name);
            const uint64_t id = data.nextAssembly++;
            data.assemblies.emplace(id, Impl::Assembly{file, name, loaded, loadedImage});
            assembly.value = id;
            CRU_INFO("[Scripting] Loaded managed assembly {} from {}", name, filename);
            return true;
        } catch (const std::filesystem::filesystem_error& exception) { return data.Fail(exception.what()); }
#else
        (void)path;
        return false;
#endif
    }

    bool ScriptRuntime::DiscoverClasses(ScriptAssemblyHandle assembly, std::vector<ScriptClassInfo>& classes) {
        classes.clear(); auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        auto* loaded = data.GetAssembly(assembly);
        if (!loaded) return false;
        GcRoot owner(reinterpret_cast<MonoObject*>(mono_assembly_get_object(data.root, loaded->assembly)));
        GcRoot exported; std::string error;
        if (!Reflect(owner.Get(), "GetExportedTypes", 0, nullptr, exported, error) || !exported.Get())
            return data.Fail("Discovering classes in " + loaded->name + ": " + error);
        auto* array = reinterpret_cast<MonoArray*>(exported.Get());
        std::vector<ScriptClassInfo> found;
        for (uintptr_t index = 0; index < mono_array_length(array); ++index) {
            GcRoot reflected(mono_array_get(array, MonoObject*, index));
            auto* type = mono_class_from_mono_type(mono_reflection_type_get_type(reinterpret_cast<MonoReflectionType*>(reflected.Get())));
            if (!type || !SupportedClass(type)) continue;
            if (!data.validatedClasses.contains(type)) {
                if (!ValidateMembers(reflected.Get(), error)) return data.Fail("Discovering members in " + loaded->name + ": " + error);
                data.validatedClasses.insert(type);
            }
            found.push_back({mono_class_get_namespace(type), mono_class_get_name(type),
                bool(mono_class_get_flags(type) & MONO_TYPE_ATTR_ABSTRACT), DefaultConstructor(type) != nullptr});
        }
        classes = std::move(found);
        return true;
#else
        (void)assembly;
        return false;
#endif
    }

    bool ScriptRuntime::CreateObject(ScriptAssemblyHandle assembly, const std::string& nameSpace,
                                     const std::string& name, ScriptObjectHandle& object) {
        object = {}; auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        auto* loaded = data.GetAssembly(assembly);
        if (!loaded) return false;
        auto* type = data.GetClass(*loaded, nameSpace, name);
        if (!type) return false;
        if (mono_class_get_flags(type) & MONO_TYPE_ATTR_ABSTRACT) return data.Fail("Cannot instantiate an abstract/static class: " + name);
        if (!DefaultConstructor(type)) return data.Fail("Class requires a public parameterless constructor: " + name);
        GcRoot reflected(reinterpret_cast<MonoObject*>(mono_type_get_object(data.root, mono_class_get_type(type))));
        auto* descriptor = mono_method_desc_new("System.Activator:CreateInstance(System.Type)", true);
        auto* constructor = descriptor ? mono_method_desc_search_in_image(descriptor, mono_get_corlib()) : nullptr;
        if (descriptor) mono_method_desc_free(descriptor);
        if (!constructor) return data.Fail("Framework Activator.CreateInstance(Type) is unavailable");
        void* arguments[] = {reflected.Get()};
        ManagedCallScope scope;
        MonoObject* exception = nullptr;
        auto* created = mono_runtime_invoke(constructor, nullptr, arguments, &exception);
        if (exception) return data.Fail("Constructing " + name + ": " + ManagedException(exception));
        GcRoot instance(created);
        if (!instance.Get()) return data.Fail("Mono could not allocate object: " + name);
        const uint32_t root = mono_gchandle_new(instance.Get(), false);
        try { object.value = data.nextObject++; data.objects.emplace(object.value, Impl::Object{root, type}); }
        catch (...) { mono_gchandle_free(root); object = {}; throw; }
        data.error.clear();
        return true;
#else
        (void)assembly; (void)nameSpace; (void)name;
        return false;
#endif
    }

    bool ScriptRuntime::DestroyObject(ScriptObjectHandle& object) {
        auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        if (!object) return true;
        const auto found = data.objects.find(object.value);
        if (found == data.objects.end()) return data.Fail("Unknown or stale managed object handle");
        mono_gchandle_free(found->second.handle);
        data.objects.erase(found); object = {};
        return true;
#else
        (void)object;
        return false;
#endif
    }

    bool ScriptRuntime::Invoke(ScriptObjectHandle object, const std::string& method,
                               const std::vector<ScriptValue>& arguments, ScriptValue& result) {
        result = std::monostate{}; auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        const auto found = data.objects.find(object.value);
        if (found == data.objects.end()) return data.Fail("Unknown or stale managed object handle");
        std::string error;
        auto* callable = FindMethod(found->second.type, method, false, arguments, error);
        if (!callable) return data.Fail(error);
        auto* target = mono_gchandle_get_target(found->second.handle);
        if (!target) return data.Fail("Managed object was released");
        if (!CallManaged(data.root, callable, target, arguments, result, error)) return data.Fail("Calling " + method + ": " + error);
        data.error.clear();
        return true;
#else
        (void)object; (void)method; (void)arguments;
        return false;
#endif
    }

    bool ScriptRuntime::InvokeStatic(ScriptAssemblyHandle assembly, const std::string& nameSpace,
                                     const std::string& name, const std::string& method,
                                     const std::vector<ScriptValue>& arguments, ScriptValue& result) {
        result = std::monostate{}; auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        auto* loaded = data.GetAssembly(assembly);
        if (!loaded) return false;
        auto* type = data.GetClass(*loaded, nameSpace, name);
        if (!type) return false;
        std::string error;
        auto* callable = FindMethod(type, method, true, arguments, error);
        if (!callable) return data.Fail(error);
        if (!CallManaged(data.root, callable, nullptr, arguments, result, error)) return data.Fail("Calling " + name + "." + method + ": " + error);
        data.error.clear();
        return true;
#else
        (void)assembly; (void)nameSpace; (void)name; (void)method; (void)arguments;
        return false;
#endif
    }

    bool ScriptRuntime::CreateInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
                                       const std::string& name, ScriptInstanceHandle& instance, bool enabled) {
        return CreateInstanceImpl(assembly, nameSpace, name, instance, enabled, nullptr);
    }

    bool ScriptRuntime::CreateEntityInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
                                            const std::string& name, uint32_t index, uint32_t generation,
                                            ScriptInstanceHandle& instance) {
        const uint32_t identity[] = {index, generation};
        if (!index) { instance = {}; return m_impl->Fail("An entity script requires a non-null owner"); }
        return CreateInstanceImpl(assembly, nameSpace, name, instance, true, identity);
    }

    bool ScriptRuntime::CreateEntityInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
                                            const std::string& name, uint32_t index, uint32_t generation,
                                            ScriptInstanceHandle& instance, const std::vector<ScriptFieldValue>& fields) {
        const uint32_t identity[] = {index, generation};
        if (!index) { instance = {}; return m_impl->Fail("An entity script requires a non-null owner"); }
        return CreateInstanceImpl(assembly, nameSpace, name, instance, true, identity, &fields);
    }

    bool ScriptRuntime::GetEntityFieldDefaults(ScriptAssemblyHandle assembly, const std::string& nameSpace,
                                              const std::string& name, std::vector<ScriptFieldValue>& fields) {
        fields.clear(); auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        ScriptAssemblyHandle core;
        if (!LoadAssembly("Managed/Crucible.ScriptCore.dll", core)) return false;
        auto* framework = data.GetAssembly(core); auto* loaded = data.GetAssembly(assembly);
        auto* base = framework ? data.GetClass(*framework, "Crucible", "EntityScript") : nullptr;
        auto* type = loaded ? data.GetClass(*loaded, nameSpace, name) : nullptr;
        if (!base || !type) return false;
        if (!mono_class_is_subclass_of(type, base, false)) return data.Fail("Public fields require an EntityScript class");
        if (const auto found = data.fieldDefaults.find(type); found != data.fieldDefaults.end()) { fields = found->second; return true; }
        if (InspectableFields(type).empty()) { data.fieldDefaults.emplace(type, fields); return true; }
        ScriptObjectHandle object;
        if (!CreateObject(assembly, nameSpace, name, object)) return false;
        bool success;
        std::string failure;
        try {
            const auto& entry = data.objects.at(object.value);
            success = ReadFields(mono_gchandle_get_target(entry.handle), type, fields, failure);
        } catch (...) { data.ReleaseObject(object); throw; }
        data.ReleaseObject(object); // inspection never invokes lifecycle callbacks
        if (!success) return data.Fail(failure);
        data.fieldDefaults.emplace(type, fields); return true;
#else
        (void)assembly; (void)nameSpace; (void)name; return false;
#endif
    }

    bool ScriptRuntime::ReadEntityFields(ScriptInstanceHandle instance, std::vector<ScriptFieldValue>& fields) {
        fields.clear(); auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        const auto* owned = data.GetInstance(instance);
        if (!owned) return false;
        if (!owned->info.entityOwned || !owned->object) return data.Fail("Entity fields require a live entity-owned script instance");
        const auto found = data.objects.find(owned->object.value);
        if (found == data.objects.end()) return data.Fail("Managed object was released");
        std::string failure;
        if (!ReadFields(mono_gchandle_get_target(found->second.handle), found->second.type, fields, failure)) return data.Fail(failure);
        return true;
#else
        (void)instance; return false;
#endif
    }

    bool ScriptRuntime::SetEntityFields(ScriptInstanceHandle instance, const std::vector<ScriptFieldValue>& fields) {
        auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        const auto* owned = data.GetInstance(instance);
        if (!owned) return false;
        if (!owned->info.entityOwned || !owned->object) return data.Fail("Entity fields require a live entity-owned script instance");
        const auto found = data.objects.find(owned->object.value);
        if (found == data.objects.end()) return data.Fail("Managed object was released");
        std::string failure;
        if (!WriteFields(data.root, mono_gchandle_get_target(found->second.handle), found->second.type, fields, failure)) return data.Fail(failure);
        return true;
#else
        (void)instance; (void)fields; return false;
#endif
    }

    bool ScriptRuntime::DiscoverEntityClasses(ScriptAssemblyHandle assembly, std::vector<ScriptClassInfo>& classes) {
        classes.clear();
        std::vector<ScriptClassInfo> candidates;
        ScriptAssemblyHandle core;
        if (!LoadAssembly("Managed/Crucible.ScriptCore.dll", core) || !DiscoverClasses(assembly, candidates)) return false;
#if CRUCIBLE_WITH_MONO
        auto& data = *m_impl;
        auto* loaded = data.GetAssembly(assembly);
        auto* framework = data.GetAssembly(core);
        auto* base = framework ? data.GetClass(*framework, "Crucible", "EntityScript") : nullptr;
        if (!base || !loaded) return false;
        std::vector<ScriptClassInfo> eligible;
        for (const auto& candidate : candidates) {
            if (candidate.isAbstract || !candidate.hasPublicDefaultConstructor) continue;
            auto* type = data.GetClass(*loaded, candidate.nameSpace, candidate.name);
            if (!type) return false;
            if (mono_class_is_subclass_of(type, base, false)) eligible.push_back(candidate);
        }
        classes = std::move(eligible);
        return true;
#else
        return false;
#endif
    }

    bool ScriptRuntime::CreateInstanceImpl(ScriptAssemblyHandle assembly, const std::string& nameSpace,
                                           const std::string& name, ScriptInstanceHandle& instance, bool enabled,
                                           const uint32_t* entityIdentity, const std::vector<ScriptFieldValue>* fields) {
        instance = {}; auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        MonoClass* base = nullptr;
        MonoMethod* bind = nullptr;
        if (entityIdentity) {
            ScriptAssemblyHandle core;
            if (!LoadAssembly("Managed/Crucible.ScriptCore.dll", core)) return false;
            auto* framework = data.GetAssembly(core);
            base = framework ? data.GetClass(*framework, "Crucible", "EntityScript") : nullptr;
            if (!base) return false;
            bind = mono_class_get_method_from_name(base, "BindEntity", 2);
            if (!bind) return data.Fail("EntityScript.BindEntity is unavailable; rebuild ScriptCore");
        }
        auto* loaded = data.GetAssembly(assembly);
        if (!loaded) return false;
        auto* type = data.GetClass(*loaded, nameSpace, name);
        if (!type) return false;
        if (base && !mono_class_is_subclass_of(type, base, false)) return data.Fail("Entity scripts must inherit Crucible.EntityScript: " + name);
        if (!entityIdentity) {
            for (auto* parent = type; parent; parent = mono_class_get_parent(parent)) {
                if (std::strcmp(mono_class_get_name(parent), "EntityScript") == 0 &&
                    std::strcmp(mono_class_get_namespace(parent), "Crucible") == 0)
                    return data.Fail("EntityScript classes require a Script component or CreateEntityInstance; they cannot run as standalone instances");
            }
        }
        std::string failure;
        auto* create = LifecycleMethod(data.root, type, "OnCreate", {}, failure);
        if (!create) return data.Fail(name + ": " + failure);
        auto* update = LifecycleMethod(data.root, type, "OnUpdate", {0.0f}, failure);
        if (!update) return data.Fail(name + ": " + failure);
        auto* destroy = LifecycleMethod(data.root, type, "OnDestroy", {}, failure);
        if (!destroy) return data.Fail(name + ": " + failure);
        ScriptObjectHandle object;
        if (!CreateObject(assembly, nameSpace, name, object)) return false;
        if (entityIdentity) {
            ScriptValue result;
            auto* target = mono_gchandle_get_target(data.objects.at(object.value).handle);
            const std::vector<ScriptValue> identity{std::bit_cast<int32_t>(entityIdentity[0]), std::bit_cast<int32_t>(entityIdentity[1])};
            if (!CallManaged(data.root, bind, target, identity, result, failure)) {
                data.ReleaseObject(object);
                return data.Fail("Binding entity: " + failure);
            }
        }
        if (fields) {
            const auto& entry = data.objects.at(object.value);
            try {
                if (!WriteFields(data.root, mono_gchandle_get_target(entry.handle), entry.type, *fields, failure)) {
                    data.ReleaseObject(object); return data.Fail(failure);
                }
            } catch (...) { data.ReleaseObject(object); throw; }
        }
        const uint64_t id = data.nextInstance++;
        Impl::Instance* owned = nullptr;
        try {
            auto [found, inserted] = data.instances.emplace(id, Impl::Instance{
                {{id}, assembly, nameSpace, name, enabled ? ScriptInstanceState::Running : ScriptInstanceState::Disabled},
                object, create, update, destroy});
            (void)inserted; owned = &found->second;
        } catch (...) { data.ReleaseObject(object); throw; }
        if (entityIdentity) {
            owned->info.entityOwned = true;
            owned->info.entityIndex = entityIdentity[0];
            owned->info.entityGeneration = entityIdentity[1];
        }
        if (!data.Callback(*owned, create, "OnCreate", {}, failure)) {
            std::string cleanup;
            if (!data.Dispose(*owned, cleanup)) failure += "\nCleanup also failed: " + cleanup;
            data.instances.erase(id);
            return data.Fail(failure);
        }
        instance.value = id;
        data.error.clear();
        CRU_INFO("[Scripting] Created lifecycle instance {}", data.Label(*owned));
        return true;
#else
        (void)assembly; (void)nameSpace; (void)name; (void)enabled; (void)entityIdentity; (void)fields;
        return false;
#endif
    }

    bool ScriptRuntime::SetInstanceEnabled(ScriptInstanceHandle instance, bool enabled) {
        auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        auto* owned = data.GetInstance(instance);
        if (!owned) return false;
        if (owned->info.state == ScriptInstanceState::Faulted) return data.Fail("Faulted instances must be destroyed and recreated");
        owned->info.state = enabled ? ScriptInstanceState::Running : ScriptInstanceState::Disabled;
        return true;
#else
        (void)instance; (void)enabled;
        return false;
#endif
    }

    bool ScriptRuntime::GetInstanceInfo(ScriptInstanceHandle instance, ScriptInstanceInfo& info) {
        info = {}; auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        const auto* owned = data.GetInstance(instance);
        if (!owned) return false;
        info = owned->info; return true;
#else
        (void)instance;
        return false;
#endif
    }

    bool ScriptRuntime::ListInstances(std::vector<ScriptInstanceInfo>& instances) {
        instances.clear(); auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        for (const auto& [id, instance] : data.instances) { (void)id; instances.push_back(instance.info); }
        return true;
#else
        return false;
#endif
    }

    bool ScriptRuntime::UpdateInstances(float deltaTime) {
        auto& data = *m_impl;
        if (!data.Ready()) return false;
        if (!std::isfinite(deltaTime) || deltaTime < 0) return data.Fail("Script delta time must be finite and nonnegative");
#if CRUCIBLE_WITH_MONO
        std::string firstError;
        for (auto& [id, instance] : data.instances) {
            (void)id;
            if (instance.info.entityOwned) continue;
            if (!data.Step(instance, deltaTime) && firstError.empty()) firstError = data.error;
        }
        data.error = std::move(firstError);
        return data.error.empty();
#else
        return false;
#endif
    }

    bool ScriptRuntime::UpdateEntityInstance(ScriptInstanceHandle instance, float deltaTime) {
        auto& data = *m_impl;
        if (!data.Ready()) return false;
        if (!std::isfinite(deltaTime) || deltaTime < 0) return data.Fail("Script delta time must be finite and nonnegative");
#if CRUCIBLE_WITH_MONO
        auto* owned = data.GetInstance(instance);
        if (!owned) return false;
        if (!owned->info.entityOwned) return data.Fail("Entity update requires an entity-owned instance");
        return data.Step(*owned, deltaTime);
#else
        (void)instance;
        return false;
#endif
    }

    bool ScriptRuntime::DestroyInstance(ScriptInstanceHandle& instance) {
        auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        if (!instance) return true;
        auto* owned = data.GetInstance(instance);
        if (!owned) return false;
        std::string failure;
        const bool success = data.Dispose(*owned, failure);
        CRU_INFO("[Scripting] Destroyed lifecycle instance {} after {} updates", data.Label(*owned), owned->info.updateCount);
        data.instances.erase(instance.value); instance = {};
        if (success) data.error.clear();
        return success || data.Fail(failure);
#else
        (void)instance;
        return false;
#endif
    }

    bool ScriptRuntime::DestroyAllInstances() {
        auto& data = *m_impl;
        if (!data.Ready()) return false;
#if CRUCIBLE_WITH_MONO
        return data.StopInstances();
#else
        return false;
#endif
    }

    bool ScriptRuntime::CreateConfiguredInstances(const ScriptingConfig& config) {
        if (!config.enabled || config.instances.empty()) return true;
        auto& data = *m_impl;
        if (!data.Ready()) return false;
        std::vector<ScriptInstanceHandle> created;
        for (const auto& definition : config.instances) {
            ScriptAssemblyHandle assembly;
            ScriptInstanceHandle instance;
            if (!LoadAssembly(definition.assembly, assembly) ||
                !CreateInstance(assembly, definition.nameSpace, definition.className, instance, definition.enabled)) {
                std::string failure = "Creating configured script '" + definition.className + "': " + data.error;
                for (auto it = created.rbegin(); it != created.rend(); ++it) {
                    if (!DestroyInstance(*it)) failure += "\nRollback cleanup failed: " + data.error;
                }
                return data.Fail(failure);
            }
            created.push_back(instance);
        }
        return true;
    }

    bool ScriptRuntime::RunStartupEntry(const ScriptingConfig& config) {
        if (!config.enabled) return true;
        if (config.startupAssembly.empty() && config.startupNamespace.empty() && config.startupClass.empty() && config.startupMethod.empty()) return true;
        if (config.startupAssembly.empty() || config.startupClass.empty() || config.startupMethod.empty())
            return m_impl->Fail("Startup entry requires assembly, class and method (namespace may be empty)");
        ScriptAssemblyHandle assembly;
        if (!LoadAssembly(config.startupAssembly, assembly)) return false;
        ScriptValue result;
        bool success;
        if (config.startupStatic) success = InvokeStatic(assembly, config.startupNamespace, config.startupClass, config.startupMethod, {}, result);
        else {
            ScriptObjectHandle object;
            if (!CreateObject(assembly, config.startupNamespace, config.startupClass, object)) return false;
            success = Invoke(object, config.startupMethod, {}, result);
            const auto error = m_impl->error;
            DestroyObject(object);
            if (!success) m_impl->error = error;
        }
        if (!success) return false;
        const auto text = std::visit([](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) return "void";
            else if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
            else if constexpr (std::is_same_v<T, std::string>) return value;
            else if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
            else return std::to_string(value);
        }, result);
        CRU_INFO("[Scripting] Startup {}.{} returned: {}", config.startupClass, config.startupMethod, text);
        return true;
    }
}

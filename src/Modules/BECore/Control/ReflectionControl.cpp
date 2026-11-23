#include "ReflectionControl.h"

#include <BECore/Control/ControlBuiltinAnswers.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <cmath>

namespace BECore {

    namespace {

        /// Bytes of a client's name that an error message quotes
        constexpr size_t kMaxShownBytes = 64;

        String Shown(StringView text) {
            if (text.size() <= kMaxShownBytes) {
                return String(text);
            }
            String shown(text.substr(0, kMaxShownBytes));
            shown += "...";
            return shown;
        }

        std::unexpected<ControlError> InvalidParams(String message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

        std::expected<String, ControlError> ReadText(const Json& params, StringView key) {
            Optional<String> text = GetJsonString(params, key);
            if (!text.has_value()) {
                return InvalidParams(Format("'{}' has to be a string", key));
            }
            return std::move(*text);
        }

        /// The `root` and the `path` every property method takes
        struct PropertyNames {
            String root;
            String path;
        };

        std::expected<PropertyNames, ControlError> ReadNames(const Json& params) {
            auto root = ReadText(params, "root");
            if (!root) {
                return std::unexpected(std::move(root.error()));
            }
            auto path = ReadText(params, "path");
            if (!path) {
                return std::unexpected(std::move(path.error()));
            }
            return PropertyNames{std::move(*root), std::move(*path)};
        }

        /// What a root names: the entry (for its options) and the object it has now
        struct Target {
            const ControlRoot* root = nullptr;
            TypedRef object;
        };

        std::expected<Target, ControlError> ResolveRoot(const ControlService& service, StringView name) {
            const ControlRoot* root = service.Roots().Find(name);
            if (root == nullptr) {
                return std::unexpected(MakeControlError(ReflectionControlErrors::kUnknownRoot, Format("There is no root '{}'", Shown(name))));
            }
            const TypedRef object = root->resolve ? root->resolve() : TypedRef{};
            if (!object) {
                return std::unexpected(MakeControlError(ReflectionControlErrors::kNoObject, Format("The root '{}' has no object now", Shown(name))));
            }
            return Target{root, object};
        }

        /// A JSON value as the value of a field: a bool, an integer (`int64_t`, or `uint64_t` above its range), a finite number or a string
        std::expected<PropertyValue, ControlError> ReadValue(const Json& params) {
            const Json* value = FindJsonMember(params, "value");
            if (value == nullptr) {
                return InvalidParams("'value' is missing");
            }
            if (value->is_boolean()) {
                return PropertyValue(std::in_place_type<bool>, *value->get_ptr<const Json::boolean_t*>());
            }
            if (value->is_number_unsigned()) {
                const uint64_t number = *value->get_ptr<const Json::number_unsigned_t*>();
                if (number <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                    return PropertyValue(std::in_place_type<int64_t>, static_cast<int64_t>(number));
                }
                return PropertyValue(std::in_place_type<uint64_t>, number);
            }
            if (value->is_number_integer()) {
                return PropertyValue(std::in_place_type<int64_t>, *value->get_ptr<const Json::number_integer_t*>());
            }
            if (value->is_number_float()) {
                const double number = *value->get_ptr<const Json::number_float_t*>();
                if (!std::isfinite(number)) {
                    return InvalidParams("'value' has to be a finite number");
                }
                return PropertyValue(std::in_place_type<double>, number);
            }
            if (value->is_string()) {
                const auto* text = value->get_ptr<const Json::string_t*>();
                return PropertyValue(std::in_place_type<String>, String(text->c_str(), text->size()));
            }
            return InvalidParams("'value' has to be a boolean, a number or a string");
        }

        /// The value as the `Json` form wrote it: the numbers through the writer's own text, a string escaped by the writer
        void WriteValue(JsonWriter& writer, const PropertyValue& value) {
            std::visit(
                [&writer](const auto& held) {
                    using Held = std::decay_t<decltype(held)>;
                    if constexpr (std::is_same_v<Held, String>) {
                        writer.Value(StringView(held));
                    } else {
                        writer.Value(held);
                    }
                },
                value);
        }

        /// The wire code of a refused access. A path over a limit is an invalid path to a client
        ControlError FromPropertyError(const PropertyError& error) {
            const PropertyErrorCode code = error.code == PropertyErrorCode::too_long ? PropertyErrorCode::invalid_path : error.code;
            return MakeControlError(EnumUtils<PropertyErrorCode>::ToString(code), error.message);
        }

        /// By hand, not a reflected struct: `value` is a `PropertyValue`, written as the bare scalar a client sent, where a reflected variant is `{index, value}`
        void WriteAccess(JsonWriter& writer, StringView root, StringView path, const PropertyValue& value) {
            writer.BeginObject();
            writer.Member("path", path);
            writer.Member("root", root);
            writer.Key("value");
            WriteValue(writer, value);
            writer.EndObject();
        }

        /// Whether a type holds a `PoolString` anywhere inside; every object type is entered once, so a type that holds its own kind ends the walk
        class InternWalk {
        public:
            explicit InternWalk(const TypeRegistry& registry) : _registry(registry) {}

            bool Reaches(const TypeDesc& desc) {
                if (desc.interns) {
                    return true;
                }
                if (desc.element != nullptr && Reaches(*desc.element)) {
                    return true;
                }
                if (desc.key != nullptr && Reaches(*desc.key)) {
                    return true;
                }
                for (const TypeDesc* alternative : desc.alternatives) {
                    if (alternative != nullptr && Reaches(*alternative)) {
                        return true;
                    }
                }
                return desc.objectMeta != nullptr && ReachesObject(desc.ObjectHash());
            }

        private:
            bool ReachesObject(uint64_t hash) {
                if (!_entered.insert(hash).second) {
                    return false;
                }
                const TypeInfo* info = _registry.Find(hash);
                if (info == nullptr) {
                    return false;
                }
                for (const FieldRef& field : _registry.AllFields(*info)) {
                    if (field.field->type != nullptr && Reaches(*field.field->type)) {
                        return true;
                    }
                }
                return false;
            }

            const TypeRegistry& _registry;
            HashSet<uint64_t> _entered;
        };

        void HandleGetProperty(ControlCall& call, const ControlService& service) {
            const auto names = ReadNames(call.Params());
            if (!names) {
                call.Fail(names.error());
                return;
            }
            const auto target = ResolveRoot(service, names->root);
            if (!target) {
                call.Fail(target.error());
                return;
            }
            const auto value = GetProperty(target->object, names->path);
            if (!value) {
                call.Fail(FromPropertyError(value.error()));
                return;
            }
            call.Reply([&](JsonWriter& writer) { WriteAccess(writer, names->root, names->path, *value); });
        }

        void HandleSetProperty(ControlCall& call, const ControlService& service) {
            const auto names = ReadNames(call.Params());
            if (!names) {
                call.Fail(names.error());
                return;
            }
            const auto value = ReadValue(call.Params());
            if (!value) {
                call.Fail(value.error());
                return;
            }
            const auto target = ResolveRoot(service, names->root);
            if (!target) {
                call.Fail(target.error());
                return;
            }
            const auto written = SetProperty(target->object, names->path, *value, SetOptions{target->root->allowInterning});
            if (!written) {
                call.Fail(FromPropertyError(written.error()));
                return;
            }
            // The answer is what the field holds now, which can differ from the request (an enum written by index reads back by name)
            const auto readBack = GetProperty(target->object, names->path);
            if (!readBack) {
                call.Fail(ControlErrors::kInternalError, Format("The value was written but cannot be read back: {}", readBack.error().message));
                return;
            }
            call.Reply([&](JsonWriter& writer) { WriteAccess(writer, names->root, names->path, *readBack); });
        }

        /// The wire code of a refused call; `CallStatus::unknown_method` is renamed so it is not taken for the control method of that name
        ControlError FromCallError(const CallError& error) {
            const StringView code = error.status == CallStatus::unknown_method ? ReflectionControlErrors::kUnknownFunction : EnumUtils<CallStatus>::ToString(error.status);
            return MakeControlError(code, error.message);
        }

        void HandleCallMethod(ControlCall& call, const ControlService& service) {
            const Json& params = call.Params();
            const auto rootName = ReadText(params, "root");
            if (!rootName) {
                call.Fail(rootName.error());
                return;
            }
            const auto method = ReadText(params, "method");
            if (!method) {
                call.Fail(method.error());
                return;
            }
            const Json* arguments = FindJsonMember(params, "arguments");
            if (arguments != nullptr && !arguments->is_object()) {
                call.Fail(ControlErrors::kInvalidParams, "'arguments' has to be an object");
                return;
            }
            const auto target = ResolveRoot(service, *rootName);
            if (!target) {
                call.Fail(target.error());
                return;
            }

            // Before the arguments are read: a method that takes text for the pool is refused whatever the arguments are
            const TypeRegistry& registry = TypeRegistry::GetInstance();
            const MethodRef found = registry.FindMethod(*target->object.type, *method);
            if (found && found.method->callable && !target->root->allowInterning && MethodInterns(*found.method, registry)) {
                call.Fail(ReflectionControlErrors::kInterning, Format("Method '{}' takes text that would be interned, and the root '{}' does not allow it", found.method->name, Shown(*rootName)));
                return;
            }

            // `Post` refuses a request nested deeper than `kMaxRequestDepth`, so the document is safe to read; a recursive parameter type is
            // still bounded by the reader's own limit on open scopes
            const Json noArguments = Json::object();
            JsonDeserializer reader(arguments != nullptr ? *arguments : noArguments);
            JsonSerializer writer;
            const auto called = CallMethod(target->object, *method, reader, writer, registry);
            if (!called) {
                call.Fail(FromCallError(called.error()));
                return;
            }

            const Json written = writer.Take();
            const auto result = written.find("result");
            ReflectionMethodAnswer answer;
            answer.method = ArenaString(method->data(), method->size());
            if (result != written.end()) {
                answer.result = JsonPayload::Borrowed(*result);
            }
            answer.root = ArenaString(rootName->data(), rootName->size());
            call.ReplyObject(answer);
        }

        /// An optional boolean member: @p fallback when absent, an `invalid_params` error when present and not a boolean
        std::expected<bool, ControlError> ReadOptionalBool(const Json& params, StringView key, bool fallback) {
            if (FindJsonMember(params, key) == nullptr) {
                return fallback;
            }
            const Optional<bool> flag = GetJsonBool(params, key);
            if (!flag.has_value()) {
                return InvalidParams(Format("'{}' has to be a boolean", key));
            }
            return *flag;
        }

        /// The type a client named; the name is looked up as text, never interned
        std::expected<const TypeInfo*, ControlError> FindType(const TypeRegistry& registry, StringView name) {
            const TypeInfo* info = registry.Find(name);
            if (info == nullptr) {
                return std::unexpected(MakeControlError(ReflectionControlErrors::kUnknownType, Format("There is no type '{}'", Shown(name))));
            }
            return info;
        }

        void HandleListTypes(ControlCall& call) {
            const Json& params = call.Params();
            const TypeRegistry& registry = TypeRegistry::GetInstance();
            ListTypesFilter filter;
            if (FindJsonMember(params, "base") != nullptr) {
                const auto baseName = ReadText(params, "base");
                if (!baseName) {
                    call.Fail(baseName.error());
                    return;
                }
                const auto base = FindType(registry, *baseName);
                if (!base) {
                    call.Fail(base.error());
                    return;
                }
                filter.base = *base;
            }
            const auto recursive = ReadOptionalBool(params, "recursive", filter.recursive);
            if (!recursive) {
                call.Fail(recursive.error());
                return;
            }
            const auto creatable = ReadOptionalBool(params, "creatable", filter.creatableOnly);
            if (!creatable) {
                call.Fail(creatable.error());
                return;
            }
            filter.recursive = *recursive;
            filter.creatableOnly = *creatable;
            call.ReplyObject(MakeListTypesAnswer(registry, filter));
        }

        void HandleDescribeType(ControlCall& call) {
            const auto name = ReadText(call.Params(), "name");
            if (!name) {
                call.Fail(name.error());
                return;
            }
            const TypeRegistry& registry = TypeRegistry::GetInstance();
            const auto info = FindType(registry, *name);
            if (!info) {
                call.Fail(info.error());
                return;
            }
            call.ReplyObject(DescribeTypeSchema(**info, registry));
        }

        void HandleSchema(ControlCall& call, const ControlService& service) {
            call.ReplyObject(MakeSchemaAnswer(TypeRegistry::GetInstance(), service.Roots()));
        }

    }  // namespace

    bool MethodInterns(const MethodDesc& method, const TypeRegistry& registry) {
        InternWalk walk(registry);
        for (const ParamDesc& param : method.params) {
            if (param.type != nullptr && walk.Reaches(*param.type)) {
                return true;
            }
        }
        return false;
    }

    void RegisterReflectionControl(ControlService& service) {
        service.RegisterHandler("get_property", [&service](ControlCall& call) { HandleGetProperty(call, service); });
        service.RegisterHandler("set_property", [&service](ControlCall& call) { HandleSetProperty(call, service); });
        service.RegisterHandler("call_method", [&service](ControlCall& call) { HandleCallMethod(call, service); });
        service.RegisterHandler("list_types", [](ControlCall& call) { HandleListTypes(call); });
        service.RegisterHandler("describe_type", [](ControlCall& call) { HandleDescribeType(call); });
        service.RegisterHandler("schema", [&service](ControlCall& call) { HandleSchema(call, service); });

        const ControlParamInfo root{"root", "string", true, "A named object, as schema lists the roots"};
        const ControlParamInfo path{"path", "string", true, "The property path from the root"};
        service.DescribeMethod("get_property", {"Reads a reflected property of a root by its path", {root, path}});
        service.DescribeMethod("set_property", {"Writes a reflected property of a root by its path", {root, path, {"value", "any", true, "A boolean, a number or a string"}}});
        service.DescribeMethod("call_method",
                               {"Calls a reflected method of a root", {root, {"method", "string", true, "The name of the method"}, {"arguments", "object", false, "The arguments by parameter name"}}});
        service.DescribeMethod("list_types", {"Lists the reflected types",
                                              {{"base", "string", false, "Only the types derived from this one"},
                                               {"recursive", "boolean", false, "With base: the whole subtree (default true), else only the direct children"},
                                               {"creatable", "boolean", false, "Only the types a client can make by name (default false)"}}});
        service.DescribeMethod("describe_type", {"The fields and methods of a reflected type", {{"name", "string", true, "The name of the type"}}});
        service.DescribeMethod("schema", {"Every reflected type, the factories and the roots (with the type each holds now), as JSON", {}});
    }

}  // namespace BECore

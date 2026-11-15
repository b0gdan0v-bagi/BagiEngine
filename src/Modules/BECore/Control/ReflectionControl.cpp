#include "ReflectionControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
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

        Json ToJson(const PropertyValue& value) {
            return std::visit(
                [](const auto& held) -> Json {
                    using Held = std::decay_t<decltype(held)>;
                    if constexpr (std::is_same_v<Held, String>) {
                        return MakeJsonString(held);
                    } else {
                        return Json(held);
                    }
                },
                value);
        }

        /// The wire code of a refused access. A path over a limit is an invalid path to a client
        ControlError FromPropertyError(const PropertyError& error) {
            const PropertyErrorCode code = error.code == PropertyErrorCode::too_long ? PropertyErrorCode::invalid_path : error.code;
            return MakeControlError(EnumUtils<PropertyErrorCode>::ToString(code), error.message);
        }

        Json DescribeAccess(StringView root, StringView path, const PropertyValue& value) {
            Json result = Json::object();
            result["root"] = MakeJsonString(root);
            result["path"] = MakeJsonString(path);
            result["value"] = ToJson(value);
            return result;
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
            call.Reply(DescribeAccess(names->root, names->path, *value));
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
            call.Reply(DescribeAccess(names->root, names->path, *readBack));
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

            Json written = writer.Take();
            Json result = Json::object();
            result["root"] = MakeJsonString(*rootName);
            result["method"] = MakeJsonString(*method);
            const auto answer = written.find("result");
            if (answer != written.end()) {
                result["result"] = std::move(*answer);
            }
            call.Reply(std::move(result));
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
    }

}  // namespace BECore

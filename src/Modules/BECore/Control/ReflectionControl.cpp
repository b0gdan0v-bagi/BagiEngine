#include "ReflectionControl.h"

#include <BECore/Control/ControlBuiltinAnswers.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlProperty.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/PropertyAccess.h>

namespace BECore {

    namespace {

        std::unexpected<ControlError> InvalidParams(String message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

        /// The `root` and the `path` every property method takes
        struct PropertyNames {
            String root;
            String path;
        };

        std::expected<PropertyNames, ControlError> ReadNames(JsonView params) {
            auto root = RequireParamString(params, "root");
            if (!root) {
                return std::unexpected(std::move(root.error()));
            }
            auto path = RequireParamString(params, "path");
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
                return std::unexpected(MakeControlError(ReflectionControlErrors::kUnknownRoot, Format("There is no root '{}'", ShownClientText(name))));
            }
            const TypedRef object = root->resolve ? root->resolve() : TypedRef{};
            if (!object) {
                return std::unexpected(MakeControlError(ReflectionControlErrors::kNoObject, Format("The root '{}' has no object now", ShownClientText(name))));
            }
            return Target{root, object};
        }

        /// By hand, not a reflected struct: `value` is a `PropertyValue`, written as the bare scalar a client sent, where a reflected variant is `{index, value}`
        void WriteAccess(JsonWriter& writer, StringView root, StringView path, const PropertyValue& value) {
            writer.BeginObject();
            writer.Member("path", path);
            writer.Member("root", root);
            writer.Key("value");
            WritePropertyValue(writer, value);
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
                call.Fail(MakePropertyControlError(value.error()));
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
            const auto value = ReadPropertyValue(call.Params());
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
                call.Fail(MakePropertyControlError(written.error()));
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
            const JsonView params = call.Params();
            const auto rootName = RequireParamString(params, "root");
            if (!rootName) {
                call.Fail(rootName.error());
                return;
            }
            const auto method = RequireParamString(params, "method");
            if (!method) {
                call.Fail(method.error());
                return;
            }
            const Optional<JsonView> arguments = FindParam(params, "arguments");
            if (arguments && !arguments->IsObject()) {
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
                call.Fail(ReflectionControlErrors::kInterning,
                          Format("Method '{}' takes text that would be interned, and the root '{}' does not allow it", found.method->name, ShownClientText(*rootName)));
                return;
            }

            // The request's own nesting limit does not bound what the reader walks (a recursive parameter type): the reader's limit on open scopes does
            JsonDeserializer reader;
            reader.Load(arguments ? *arguments : EmptyParamsView());
            String resultText;
            JsonWriter resultWriter(resultText);
            JsonStreamSerializer writer(resultWriter);
            const auto called = CallMethod(target->object, *method, reader, writer, registry);
            if (!called) {
                call.Fail(FromCallError(called.error()));
                return;
            }
            writer.Finish();

            // The result is the member `result` of the written object, which a method that returns nothing does not write
            JsonTape resultTape;
            if (!resultWriter.IsComplete() || !resultTape.Parse(resultText)) {
                call.Fail(ControlErrors::kInternalError, "The result of the method cannot be written");
                return;
            }
            const Optional<JsonView> result = JsonView(resultTape, 0).Find("result");
            ReflectionMethodAnswer answer;
            answer.method = TextView(*method);
            if (result) {
                answer.result = JsonPayload::Text(result->Raw());
            }
            answer.root = TextView(*rootName);
            call.ReplyObject(answer);
        }

        /// An optional boolean member: @p fallback when absent, an `invalid_params` error when present and not a boolean
        std::expected<bool, ControlError> ReadOptionalBool(JsonView params, StringView key, bool fallback) {
            if (!FindParam(params, key)) {
                return fallback;
            }
            const Optional<bool> flag = GetParamBool(params, key);
            if (!flag.has_value()) {
                return InvalidParams(Format("'{}' has to be a boolean", key));
            }
            return *flag;
        }

        /// The type a client named; the name is looked up as text, never interned
        std::expected<const TypeInfo*, ControlError> FindType(const TypeRegistry& registry, StringView name) {
            const TypeInfo* info = registry.Find(name);
            if (info == nullptr) {
                return std::unexpected(MakeControlError(ReflectionControlErrors::kUnknownType, Format("There is no type '{}'", ShownClientText(name))));
            }
            return info;
        }

        void HandleListTypes(ControlCall& call) {
            const JsonView params = call.Params();
            const TypeRegistry& registry = TypeRegistry::GetInstance();
            ListTypesFilter filter;
            if (FindParam(params, "base")) {
                const auto baseName = RequireParamString(params, "base");
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
            const auto name = RequireParamString(call.Params(), "name");
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
        service.DescribeMethod("get_property", {"Reads a reflected property of a root by its path", {root, path}, /*quiet=*/true});
        service.DescribeMethod("set_property", {"Writes a reflected property of a root by its path", {root, path, {"value", "any", true, "A boolean, a number or a string"}}});
        service.DescribeMethod("call_method",
                               {"Calls a reflected method of a root", {root, {"method", "string", true, "The name of the method"}, {"arguments", "object", false, "The arguments by parameter name"}}});
        service.DescribeMethod("list_types", {"Lists the reflected types",
                                              {{"base", "string", false, "Only the types derived from this one"},
                                               {"recursive", "boolean", false, "With base: the whole subtree (default true), else only the direct children"},
                                               {"creatable", "boolean", false, "Only the types a client can make by name (default false)"}},
                                              /*quiet=*/true});
        service.DescribeMethod("describe_type", {"The fields and methods of a reflected type", {{"name", "string", true, "The name of the type"}}, /*quiet=*/true});
        service.DescribeMethod("schema", {"Every reflected type, the factories and the roots (with the type each holds now), as JSON", {}, /*quiet=*/true});
    }

}  // namespace BECore

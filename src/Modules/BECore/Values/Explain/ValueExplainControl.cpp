#include "ValueExplainControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Control/ReflectionControl.h>
#include <BECore/Reflection/CompiledPath.h>
#include <BECore/Values/Explain/ValueContextRegistry.h>
#include <BECore/Values/Explain/ValueExplain.h>

namespace BECore {

    namespace {

        /// Bytes of a client's name that an error message quotes
        constexpr size_t kMaxShownBytes = 64;
        constexpr StringView kNoContextWhy = "no context was named and none is registered as 'default'";

        String Shown(StringView text) {
            if (text.size() <= kMaxShownBytes) {
                return String(text);
            }
            String shown(text.substr(0, kMaxShownBytes));
            shown += "...";
            return shown;
        }

        String Joined(const Vector<String>& names) {
            String joined;
            for (const String& name : names) {
                if (!joined.empty()) {
                    joined += ", ";
                }
                joined += name;
            }
            return joined.empty() ? String("none") : joined;
        }

        /// A mistake of the request: a name that is not text
        std::expected<String, ControlError> ReadText(JsonView params, StringView key) {
            Optional<String> text = GetParamString(params, key);
            if (!text.has_value()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' has to be a string", key)));
            }
            return std::move(*text);
        }

        /// The wire code of a refused path, as the by-name methods give it
        ControlError FromPropertyError(const PropertyError& error) {
            const PropertyErrorCode code = error.code == PropertyErrorCode::too_long ? PropertyErrorCode::invalid_path : error.code;
            return MakeControlError(EnumUtils<PropertyErrorCode>::ToString(code), error.message);
        }

        /// The `Value<T>` the request names: its address and what explains it
        struct ValueTarget {
            const void* value = nullptr;
            ValueExplainFn explain = nullptr;
        };

        std::expected<ValueTarget, ControlError> FindValue(const ControlService& service, StringView rootName, StringView path) {
            const ControlRoot* root = service.Roots().Find(rootName);
            if (root == nullptr) {
                return std::unexpected(MakeControlError(ReflectionControlErrors::kUnknownRoot, Format("There is no root '{}'", Shown(rootName))));
            }
            const TypedRef object = root->resolve ? root->resolve() : TypedRef{};
            if (!object) {
                return std::unexpected(MakeControlError(ReflectionControlErrors::kNoObject, Format("The root '{}' has no object now", Shown(rootName))));
            }
            const auto compiled = CompilePath(*object.type, path);
            if (!compiled) {
                return std::unexpected(FromPropertyError(compiled.error()));
            }
            if (!compiled->IsFixed()) {
                return std::unexpected(
                    MakeControlError(ValueExplainControlErrors::kNotFixed, Format("'{}' goes through an element or a pointee: explain_value needs a field at a fixed place", Shown(path))));
            }
            const ValueExplainFn explain = compiled->type != nullptr && compiled->type->opaqueHash != 0 ? ValueExplainers::GetInstance().Find(compiled->type->opaqueHash) : nullptr;
            if (explain == nullptr) {
                return std::unexpected(MakeControlError(ValueExplainControlErrors::kNotAValue, Format("'{}' is not a Value of a type this build can explain; the types are: {}", Shown(path),
                                                                                                      Joined(ValueExplainers::GetInstance().Names()))));
            }
            return ValueTarget{static_cast<const char*>(object.ConstAddress()) + compiled->offset, explain};
        }

        // By hand on purpose: the tree of a long loop nests deeper than `JsonWriter::kMaxDepth`, which a reflected node (the generated writer
        // asserts there) cannot take. A branch deeper than the writer is written as `null`, as `WriteJson` cuts a container that would pass it;
        // the keys are in alphabetical order, as the DOM wrote them.

        /// A container that would pass the depth of the writer is written as `null` and its children are only counted, so the budget and the cut
        /// flag do not depend on how deep the writer is
        JsonWriter* OpenContainer(JsonWriter* writer) {
            if (writer == nullptr) {
                return nullptr;
            }
            if (writer->GetDepth() >= JsonWriter::kMaxDepth) {
                writer->Null();
                return nullptr;
            }
            return writer;
        }

        /// The tree as nested `{children, label, source, type, value}`; @p budget counts the nodes still allowed, and a cut sets @p cut.
        /// A null @p writer walks the node for the budget and the cut alone
        void WriteNode(JsonWriter* writer, const ProgramTrace& trace, uint32_t index, uint32_t& budget, bool& cut) {
            const TraceNode& node = trace.nodes[index];
            JsonWriter* object = OpenContainer(writer);
            if (object != nullptr) {
                object->BeginObject();
                object->Key("children");
            }
            JsonWriter* children = OpenContainer(object);
            if (children != nullptr) {
                children->BeginArray();
            }
            for (const uint32_t child : trace.Children(node)) {
                if (budget == 0) {
                    cut = true;
                    break;
                }
                --budget;
                WriteNode(children, trace, child, budget, cut);
            }
            if (children != nullptr) {
                children->EndArray();
            }
            if (object != nullptr) {
                object->Member("label", node.label);
                object->Member("source", EnumUtils<TraceStepKind>::ToString(node.kind));
                object->Member("type", EnumUtils<ProgramType>::ToString(node.type));
                object->Member("value", FormatNodeValue(node));
                object->EndObject();
            }
        }

        void WriteExplanation(JsonWriter& writer, const ValueExplanation& explanation) {
            writer.BeginObject();
            writer.Member("has_value", explanation.hasValue);
            writer.Member("kind", EnumUtils<ValueKind>::ToString(explanation.kind));
            writer.Member("source", explanation.source);
            bool cut = false;
            if (explanation.trace.root != kNoTraceNode && explanation.trace.root < explanation.trace.nodes.size()) {
                uint32_t budget = kMaxFormattedLines - 1;
                writer.Key("trace");
                WriteNode(&writer, explanation.trace, explanation.trace.root, budget, cut);
                if (cut || explanation.trace.truncated) {
                    writer.Member("truncated", true);
                }
            }
            if (explanation.hasValue) {
                writer.Member("value", explanation.valueText);
            } else {
                writer.Member("why", explanation.why);
            }
            writer.EndObject();
        }

        void HandleExplainValue(ControlCall& call, const ControlService& service) {
            const JsonView params = call.Params();
            const auto rootName = ReadText(params, "root");
            if (!rootName) {
                call.Fail(rootName.error());
                return;
            }
            const auto path = ReadText(params, "path");
            if (!path) {
                call.Fail(path.error());
                return;
            }
            Optional<String> contextName;
            if (FindParam(params, "context")) {
                const auto named = ReadText(params, "context");
                if (!named) {
                    call.Fail(named.error());
                    return;
                }
                contextName = *named;
            }
            const auto target = FindValue(service, *rootName, *path);
            if (!target) {
                call.Fail(target.error());
                return;
            }

            const ValueContextRegistry& contexts = ValueContextRegistry::GetInstance();
            const ExplainContextProvider* provider = contexts.Find(contextName.has_value() ? StringView(*contextName) : kDefaultContextName);
            if (provider == nullptr && contextName.has_value()) {
                call.Fail(MakeControlError(ValueExplainControlErrors::kUnknownContext, Format("There is no context '{}'; the contexts are: {}", Shown(*contextName), Joined(contexts.Names()))));
                return;
            }
            Optional<ExplainContext> context;
            if (provider != nullptr && *provider) {
                context = (*provider)();
            }
            if (!context.has_value() || context->declaration == nullptr) {
                // Only the kind and the source: an empty context makes the explanation say what it lacks, which is no value here
                const ContextDecl noDeclaration;
                ValueExplanation explanation = target->explain(target->value, Context(), noDeclaration);
                explanation.hasValue = false;
                explanation.valueText.clear();
                explanation.trace = ProgramTrace();
                explanation.why = provider == nullptr ? String(kNoContextWhy) : Format("the context '{}' has no objects now", contextName.value_or(String(kDefaultContextName)));
                call.Reply([&](JsonWriter& writer) { WriteExplanation(writer, explanation); });
                return;
            }
            const ValueExplanation explanation = target->explain(target->value, context->context, *context->declaration);
            call.Reply([&](JsonWriter& writer) { WriteExplanation(writer, explanation); });
        }

    }  // namespace

    void RegisterValueExplainControl(ControlService& service) {
        RegisterBuiltinValueExplainers();
        service.RegisterHandler("explain_value", [&service](ControlCall& call) { HandleExplainValue(call, service); });
        service.DescribeMethod("explain_value",
                               {"Where a Value field of a root gets its value: its kind and source and, with a context, the value now and the trace of the formula",
                                {{"root", "string", true, "A named object, as schema lists the roots"},
                                 {"path", "string", true, "The path of a Value field from the root, at a fixed place (no element, no pointee)"},
                                 {"context", "string", false, "The name of a registered context; default is the one named 'default', and without it only kind and source are answered"}}});
    }

}  // namespace BECore

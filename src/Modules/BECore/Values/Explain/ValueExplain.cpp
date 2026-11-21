#include "ValueExplain.h"

#include <BECore/Values/Programs/Opcode.h>

namespace BECore {

    namespace Detail {

        String FormatOpaqueBytes(const TypeDesc& type, const void* bytes, size_t size) {
            const StringView name = type.opaqueName.empty() ? StringView("value") : type.opaqueName;
            return Format("<{} {} bytes: {}>", name, size, FormatHexBytes(bytes, size));
        }

        String MissingLevelText(const ContextDecl& declaration, LevelIndex level) {
            if (level < declaration.Size()) {
                return Format("the context has no object at level '{}'", declaration.Levels()[level].name);
            }
            return Format("the context has no object at level {}", level);
        }

        ProgramTrace SlotTrace(StringView path, ProgramType type, const RegisterImage& image, const TypeDesc* described) {
            TraceNode node;
            node.kind = TraceStepKind::ContextKey;
            node.type = type;
            node.label = String(path);
            node.image = image;
            node.described = described;
            ProgramTrace trace;
            trace.nodes.push_back(node);
            trace.root = 0;
            trace.resultType = type;
            trace.result = image;
            trace.resultDescribed = described;
            return trace;
        }

        void NameResultType(ProgramTrace& trace, const TypeDesc& described) {
            if (trace.resultType != ProgramType::Enum && trace.resultType != ProgramType::Domain) {
                return;
            }
            if (trace.resultDescribed == nullptr) {
                trace.resultDescribed = &described;
            }
            if (trace.root != kNoTraceNode && trace.nodes[trace.root].described == nullptr) {
                trace.nodes[trace.root].described = &described;
            }
        }

    }  // namespace Detail

    ValueExplainFn ValueExplainers::Find(uint64_t opaqueHash) const {
        LockGuard lock(_mutex);
        const auto found = _explainers.find(opaqueHash);
        return found != _explainers.end() ? found->second : nullptr;
    }

    void ValueExplainers::Add(uint64_t opaqueHash, ValueExplainFn explain) {
        LockGuard lock(_mutex);
        _explainers[opaqueHash] = explain;
    }

    void RegisterBuiltinValueExplainers() {
        ValueExplainers& explainers = ValueExplainers::GetInstance();
#define BE_REGISTER_EXPLAINER(Op, Name, Type) explainers.Register<Type>();
        BE_PROGRAM_TYPES_SCALARS(BE_REGISTER_EXPLAINER, Unused)
        BE_PROGRAM_TYPES_MATH(BE_REGISTER_EXPLAINER, Unused)
        BE_PROGRAM_TYPES_STRING(BE_REGISTER_EXPLAINER, Unused)
#undef BE_REGISTER_EXPLAINER
        explainers.Register<Rect>();
    }

}  // namespace BECore

#include "TypeDesc.h"

namespace BECore {

    StringView TypeKindName(TypeKind kind) {
        switch (kind) {
            case TypeKind::Bool:
                return "bool";
            case TypeKind::Int8:
                return "int8";
            case TypeKind::Int16:
                return "int16";
            case TypeKind::Int32:
                return "int32";
            case TypeKind::Int64:
                return "int64";
            case TypeKind::UInt8:
                return "uint8";
            case TypeKind::UInt16:
                return "uint16";
            case TypeKind::UInt32:
                return "uint32";
            case TypeKind::UInt64:
                return "uint64";
            case TypeKind::Float:
                return "float";
            case TypeKind::Double:
                return "double";
            case TypeKind::String:
                return "string";
            case TypeKind::Enum:
                return "enum";
            case TypeKind::Object:
                return "object";
            case TypeKind::List:
                return "list";
            case TypeKind::Map:
                return "map";
            case TypeKind::Optional:
                return "optional";
            case TypeKind::Variant:
                return "variant";
            case TypeKind::Pair:
                return "pair";
            case TypeKind::Pointer:
                return "pointer";
            case TypeKind::Opaque:
                return "opaque";
            case TypeKind::Void:
                return "void";
            case TypeKind::Unknown:
                return "unknown";
        }
        return "unknown";
    }

    namespace {

        bool SameOptional(const TypeDesc* a, const TypeDesc* b) {
            if (a == nullptr || b == nullptr) {
                return a == b;
            }
            return SameType(*a, *b);
        }

    }  // namespace

    bool SameType(const TypeDesc& a, const TypeDesc& b) {
        if (&a == &b) {
            return true;
        }
        if (a.kind != b.kind || a.resizable != b.resizable || a.capacity != b.capacity || a.interns != b.interns || a.ObjectHash() != b.ObjectHash() || a.opaqueSize != b.opaqueSize ||
            a.opaqueHash != b.opaqueHash) {
            return false;
        }
        if (a.names.size() != b.names.size() || a.alternatives.size() != b.alternatives.size()) {
            return false;
        }
        for (size_t i = 0; i < a.names.size(); ++i) {
            if (a.names[i] != b.names[i]) {
                return false;
            }
        }
        for (size_t i = 0; i < a.alternatives.size(); ++i) {
            if (!SameType(*a.alternatives[i], *b.alternatives[i])) {
                return false;
            }
        }
        return SameOptional(a.element, b.element) && SameOptional(a.key, b.key);
    }

}  // namespace BECore

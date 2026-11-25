#include "PanelBindingReader.h"

#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Values/Programs/Lists/ListKernels.h>
#include <bit>

namespace BECore {

    namespace {

        static_assert(std::endian::native == std::endian::little, "an enum is read from its first bytes, which are the low ones only on a little-endian host");

        constexpr StringView kTrueText = "true";
        constexpr StringView kFalseText = "false";

        template <typename T>
        T Load(const uint8_t* address) {
            T value;
            std::memcpy(&value, address, sizeof(T));
            return value;
        }

        bool IsNumberKind(TypeKind kind) {
            switch (kind) {
                case TypeKind::Int8:
                case TypeKind::Int16:
                case TypeKind::Int32:
                case TypeKind::Int64:
                case TypeKind::UInt8:
                case TypeKind::UInt16:
                case TypeKind::UInt32:
                case TypeKind::UInt64:
                case TypeKind::Float:
                case TypeKind::Double:
                    return true;
                default:
                    return false;
            }
        }

        double NumberAt(TypeKind kind, const uint8_t* address) {
            switch (kind) {
                case TypeKind::Int8:
                    return static_cast<double>(Load<int8_t>(address));
                case TypeKind::Int16:
                    return static_cast<double>(Load<int16_t>(address));
                case TypeKind::Int32:
                    return static_cast<double>(Load<int32_t>(address));
                case TypeKind::Int64:
                    return static_cast<double>(Load<int64_t>(address));
                case TypeKind::UInt8:
                    return static_cast<double>(Load<uint8_t>(address));
                case TypeKind::UInt16:
                    return static_cast<double>(Load<uint16_t>(address));
                case TypeKind::UInt32:
                    return static_cast<double>(Load<uint32_t>(address));
                case TypeKind::UInt64:
                    return static_cast<double>(Load<uint64_t>(address));
                case TypeKind::Float:
                    return static_cast<double>(Load<float>(address));
                default:
                    return Load<double>(address);
            }
        }

        /// A number as text in its own type, so a float shows `0.1` and not the digits of its double
        void AppendNumber(String& out, TypeKind kind, const uint8_t* address) {
            switch (kind) {
                case TypeKind::Int8:
                    AppendFormat(out, "{}", static_cast<int32_t>(Load<int8_t>(address)));
                    break;
                case TypeKind::Int16:
                    AppendFormat(out, "{}", Load<int16_t>(address));
                    break;
                case TypeKind::Int32:
                    AppendFormat(out, "{}", Load<int32_t>(address));
                    break;
                case TypeKind::Int64:
                    AppendFormat(out, "{}", Load<int64_t>(address));
                    break;
                case TypeKind::UInt8:
                    AppendFormat(out, "{}", static_cast<uint32_t>(Load<uint8_t>(address)));
                    break;
                case TypeKind::UInt16:
                    AppendFormat(out, "{}", Load<uint16_t>(address));
                    break;
                case TypeKind::UInt32:
                    AppendFormat(out, "{}", Load<uint32_t>(address));
                    break;
                case TypeKind::UInt64:
                    AppendFormat(out, "{}", Load<uint64_t>(address));
                    break;
                case TypeKind::Float:
                    AppendFormat(out, "{}", Load<float>(address));
                    break;
                default:
                    AppendFormat(out, "{}", Load<double>(address));
                    break;
            }
        }

        /// The name of the enumerator at @p address; empty when the stored value is not one of them
        StringView EnumNameAt(const TypeDesc& type, const uint8_t* address) {
            uint64_t index = 0;
            std::memcpy(&index, address, std::min(type.enumBytes, sizeof(index)));
            return index < type.names.size() ? type.names[static_cast<size_t>(index)] : StringView{};
        }

        /// What a read through `GetProperty` could not give: a path that leads nowhere now against a value of another kind
        PanelReadStatus StatusOf(PropertyErrorCode code) {
            return code == PropertyErrorCode::not_found ? PanelReadStatus::NoValue : PanelReadStatus::WrongType;
        }

    }  // namespace

    const PanelBindingReader::Compiled* PanelBindingReader::Compile(const TypeInfo& type, StringView path) {
        const auto byType = _compiled.find(&type);
        if (byType != _compiled.end()) {
            const auto found = byType->second.find(path);
            if (found != byType->second.end()) {
                return &found->second;
            }
        }

        if (_compiledCount >= kMaxCompiledPaths) {
            Clear();
        }
        Compiled entry;
        auto compiled = CompilePath(type, path);
        if (compiled.has_value()) {
            entry.valid = true;
            entry.path = std::move(*compiled);
        }
        const auto inserted = _compiled[&type].emplace(String(path), std::move(entry));
        ++_compiledCount;
        return &inserted.first->second;
    }

    PanelBindingReader::Located PanelBindingReader::Locate(TypedRef object, StringView path) {
        Located at;
        if (!object) {
            return at;
        }
        const Compiled* compiled = Compile(*object.type, path);
        if (!compiled->valid) {
            at.status = PanelReadStatus::BadPath;
            return at;
        }
        at.status = PanelReadStatus::Ok;
        at.object = object;
        at.writable = !object.isConst;
        at.path = &compiled->path;
        if (compiled->path.IsFixed()) {
            at.address = static_cast<const uint8_t*>(object.ConstAddress()) + compiled->path.offset;
            at.type = compiled->path.type;
        }
        return at;
    }

    PanelBindingReader::Located PanelBindingReader::LocateBinding(const PanelDataSources& sources, const PanelBinding& binding) {
        const TypedRef object = sources.Find(binding.source);
        if (!object) {
            Located at;
            at.status = sources.Contains(binding.source) ? PanelReadStatus::NoObject : PanelReadStatus::UnknownSource;
            return at;
        }
        return Locate(object, binding.path);
    }

    PanelReadStatus PanelBindingReader::FindList(const PanelDataSources& sources, const PanelBinding& list, ListView& out) {
        const Located at = LocateBinding(sources, list);
        if (at.status != PanelReadStatus::Ok) {
            return at.status;
        }
        if (at.address == nullptr || at.type->kind != TypeKind::List) {
            return PanelReadStatus::NotAList;
        }
        const auto descriptor = MakeListDescriptor(0, *at.type);
        if (!descriptor.has_value()) {
            return PanelReadStatus::NotAList;
        }
        const ListImage image = Kernel::ReadList(at.address, *descriptor);
        out = ListView{image.data, image.count, image.stride, at.type->element, at.writable};
        return PanelReadStatus::Ok;
    }

    PanelBindingReader::Located PanelBindingReader::LocateCell(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, PanelReadStatus& status) {
        Located at;
        ListView view;
        status = FindList(sources, list, view);
        if (status != PanelReadStatus::Ok) {
            at.status = status;
            return at;
        }
        if (row >= view.count || view.element == nullptr) {
            status = PanelReadStatus::NoRow;
            at.status = status;
            return at;
        }
        const uint8_t* element = view.data + row * view.stride;
        if (relativePath.empty()) {
            at.status = PanelReadStatus::Ok;
            at.address = element;
            at.type = view.element;
            at.writable = view.writable;
            return at;
        }
        const TypeInfo* info = view.element->kind == TypeKind::Object ? TypeRegistry::GetInstance().Find(view.element->ObjectHash()) : nullptr;
        if (info == nullptr) {
            status = PanelReadStatus::WrongType;
            at.status = status;
            return at;
        }
        // The reader only reads the cell; the writer may change it when the list's own object is not const
        at = Locate(TypedRef{const_cast<uint8_t*>(element), info, !view.writable}, relativePath);
        status = at.status;
        return at;
    }

    PanelReadStatus PanelBindingReader::AsBool(const Located& at, bool& out) {
        if (at.address != nullptr && at.type->kind != TypeKind::Optional) {
            if (at.type->kind != TypeKind::Bool) {
                return PanelReadStatus::WrongType;
            }
            out = Load<bool>(at.address);
            return PanelReadStatus::Ok;
        }
        if (at.path == nullptr) {
            return PanelReadStatus::WrongType;
        }
        const auto value = ReadCompiled(*at.path, at.object);
        if (!value.has_value()) {
            return StatusOf(value.error().code);
        }
        const bool* flag = std::get_if<bool>(&*value);
        if (flag == nullptr) {
            return PanelReadStatus::WrongType;
        }
        out = *flag;
        return PanelReadStatus::Ok;
    }

    PanelReadStatus PanelBindingReader::AsNumber(const Located& at, double& out) {
        if (at.address != nullptr && at.type->kind != TypeKind::Optional) {
            if (!IsNumberKind(at.type->kind)) {
                return PanelReadStatus::WrongType;
            }
            out = NumberAt(at.type->kind, at.address);
            return PanelReadStatus::Ok;
        }
        if (at.path == nullptr) {
            return PanelReadStatus::WrongType;
        }
        const auto value = ReadCompiled(*at.path, at.object);
        if (!value.has_value()) {
            return StatusOf(value.error().code);
        }
        if (const int64_t* signedValue = std::get_if<int64_t>(&*value)) {
            out = static_cast<double>(*signedValue);
        } else if (const uint64_t* unsignedValue = std::get_if<uint64_t>(&*value)) {
            out = static_cast<double>(*unsignedValue);
        } else if (const double* floating = std::get_if<double>(&*value)) {
            out = *floating;
        } else {
            return PanelReadStatus::WrongType;
        }
        return PanelReadStatus::Ok;
    }

    PanelReadStatus PanelBindingReader::AsText(const Located& at, String& scratch, StringView& out) {
        // GetProperty reads an Optional field as the value it holds, which only it can reach
        if (at.address != nullptr && at.type->kind != TypeKind::Optional) {
            const TypeDesc& type = *at.type;
            if (type.kind == TypeKind::String && type.isEngineString) {
                out = StringView(*reinterpret_cast<const String*>(at.address));
                return PanelReadStatus::Ok;
            }
            if (type.kind == TypeKind::String && type.interns) {
                out = reinterpret_cast<const PoolString*>(at.address)->ToStringView();
                return PanelReadStatus::Ok;
            }
            if (type.kind == TypeKind::Bool) {
                out = Load<bool>(at.address) ? kTrueText : kFalseText;
                return PanelReadStatus::Ok;
            }
            if (type.kind == TypeKind::Enum) {
                out = EnumNameAt(type, at.address);
                return PanelReadStatus::Ok;
            }
            if (IsNumberKind(type.kind)) {
                scratch.clear();
                AppendNumber(scratch, type.kind, at.address);
                out = StringView(scratch);
                return PanelReadStatus::Ok;
            }
            // Any other text (a FixedString) is told from a String by nothing in its descriptor: GetProperty reads it as what it is
            if (type.kind != TypeKind::String) {
                return PanelReadStatus::WrongType;
            }
        }
        if (at.path == nullptr) {
            return PanelReadStatus::WrongType;
        }
        const auto value = ReadCompiled(*at.path, at.object);
        if (!value.has_value()) {
            return StatusOf(value.error().code);
        }
        scratch.clear();
        if (const bool* flag = std::get_if<bool>(&*value)) {
            out = *flag ? kTrueText : kFalseText;
            return PanelReadStatus::Ok;
        }
        if (const int64_t* signedValue = std::get_if<int64_t>(&*value)) {
            AppendFormat(scratch, "{}", *signedValue);
        } else if (const uint64_t* unsignedValue = std::get_if<uint64_t>(&*value)) {
            AppendFormat(scratch, "{}", *unsignedValue);
        } else if (const double* floating = std::get_if<double>(&*value)) {
            AppendFormat(scratch, "{}", *floating);
        } else if (const String* text = std::get_if<String>(&*value)) {
            scratch.assign(*text);
        }
        out = StringView(scratch);
        return PanelReadStatus::Ok;
    }

    PanelReadStatus PanelBindingReader::ReadBool(const PanelDataSources& sources, const PanelBinding& binding, bool& out) {
        const Located at = LocateBinding(sources, binding);
        if (at.status != PanelReadStatus::Ok) {
            return at.status;
        }
        bool value = false;
        const PanelReadStatus status = AsBool(at, value);
        if (status == PanelReadStatus::Ok) {
            out = value != binding.negated;
        }
        return status;
    }

    PanelReadStatus PanelBindingReader::ReadNumber(const PanelDataSources& sources, const PanelBinding& binding, double& out) {
        const Located at = LocateBinding(sources, binding);
        return at.status != PanelReadStatus::Ok ? at.status : AsNumber(at, out);
    }

    PanelReadStatus PanelBindingReader::ReadText(const PanelDataSources& sources, const PanelBinding& binding, String& scratch, StringView& out) {
        const Located at = LocateBinding(sources, binding);
        return at.status != PanelReadStatus::Ok ? at.status : AsText(at, scratch, out);
    }

    PanelReadStatus PanelBindingReader::ListCount(const PanelDataSources& sources, const PanelBinding& list, size_t& count) {
        ListView view;
        const PanelReadStatus status = FindList(sources, list, view);
        if (status == PanelReadStatus::Ok) {
            count = view.count;
        }
        return status;
    }

    PanelReadStatus PanelBindingReader::ReadRowText(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, String& scratch, StringView& out) {
        PanelReadStatus status = PanelReadStatus::Ok;
        const Located at = LocateCell(sources, list, row, relativePath, status);
        return status != PanelReadStatus::Ok ? status : AsText(at, scratch, out);
    }

    PanelReadStatus PanelBindingReader::ReadRowBool(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, bool& out) {
        PanelReadStatus status = PanelReadStatus::Ok;
        const Located at = LocateCell(sources, list, row, relativePath, status);
        return status != PanelReadStatus::Ok ? status : AsBool(at, out);
    }

    void PanelBindingReader::Clear() {
        _compiled.clear();
        _compiledCount = 0;
    }

}  // namespace BECore

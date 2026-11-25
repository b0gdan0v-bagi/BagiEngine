#include "PanelBindingWriter.h"

#include <cmath>

namespace BECore {

    namespace {

        template <typename T>
        void Store(uint8_t* address, T value) {
            std::memcpy(address, &value, sizeof(T));
        }

        /// @p value rounded to the nearest integer and held at the ends of T. The limits are compared as doubles: for a 64-bit T the
        /// double of `max` is 2^63 or 2^64, so `>=` is the test that keeps the cast below it defined
        template <typename T>
        T ToInteger(double value) {
            const double rounded = std::round(value);
            if (rounded <= static_cast<double>(std::numeric_limits<T>::lowest())) {
                return std::numeric_limits<T>::lowest();
            }
            if (rounded >= static_cast<double>(std::numeric_limits<T>::max())) {
                return std::numeric_limits<T>::max();
            }
            return static_cast<T>(rounded);
        }

        float ToFloat(double value) {
            constexpr double kLimit = std::numeric_limits<float>::max();
            return static_cast<float>(std::fmax(-kLimit, std::fmin(kLimit, value)));
        }

        constexpr size_t kMaxEnumBytes = sizeof(uint64_t);

    }  // namespace

    uint8_t* PanelBindingWriter::WritableAddress(const PanelBindingReader::Located& at) {
        // The object is not const (checked through `writable`), and the address is a field of it
        return at.address != nullptr && at.writable ? const_cast<uint8_t*>(at.address) : nullptr;
    }

    PanelReadStatus PanelBindingWriter::Assign(const PanelBindingReader::Located& at, StringView text) {
        if (at.address == nullptr || at.type->kind != TypeKind::String || !at.type->isEngineString || !at.writable) {
            return PanelReadStatus::WrongType;
        }
        // The object is not const (checked above), and the field is a String at this address
        String& field = *reinterpret_cast<String*>(const_cast<uint8_t*>(at.address));
        field.assign(text.data(), text.size());
        return PanelReadStatus::Ok;
    }

    PanelReadStatus PanelBindingWriter::AssignBool(const PanelBindingReader::Located& at, bool value) {
        uint8_t* address = WritableAddress(at);
        if (address == nullptr || at.type->kind != TypeKind::Bool) {
            return PanelReadStatus::WrongType;
        }
        Store(address, value);
        return PanelReadStatus::Ok;
    }

    PanelReadStatus PanelBindingWriter::AssignNumber(const PanelBindingReader::Located& at, double value) {
        uint8_t* address = WritableAddress(at);
        if (address == nullptr) {
            return PanelReadStatus::WrongType;
        }
        // The kind is checked first so a NaN written to a text is WrongType, as every other wrong write
        switch (at.type->kind) {
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
                break;
            default:
                return PanelReadStatus::WrongType;
        }
        if (!std::isfinite(value)) {
            return PanelReadStatus::BadValue;
        }
        switch (at.type->kind) {
            case TypeKind::Int8:
                Store(address, ToInteger<int8_t>(value));
                break;
            case TypeKind::Int16:
                Store(address, ToInteger<int16_t>(value));
                break;
            case TypeKind::Int32:
                Store(address, ToInteger<int32_t>(value));
                break;
            case TypeKind::Int64:
                Store(address, ToInteger<int64_t>(value));
                break;
            case TypeKind::UInt8:
                Store(address, ToInteger<uint8_t>(value));
                break;
            case TypeKind::UInt16:
                Store(address, ToInteger<uint16_t>(value));
                break;
            case TypeKind::UInt32:
                Store(address, ToInteger<uint32_t>(value));
                break;
            case TypeKind::UInt64:
                Store(address, ToInteger<uint64_t>(value));
                break;
            case TypeKind::Float:
                Store(address, ToFloat(value));
                break;
            default:
                Store(address, value);
                break;
        }
        return PanelReadStatus::Ok;
    }

    PanelReadStatus PanelBindingWriter::AssignEnumIndex(const PanelBindingReader::Located& at, size_t index) {
        uint8_t* address = WritableAddress(at);
        if (address == nullptr || at.type->kind != TypeKind::Enum || at.type->enumBytes == 0 || at.type->enumBytes > kMaxEnumBytes) {
            return PanelReadStatus::WrongType;
        }
        if (index >= at.type->names.size()) {
            return PanelReadStatus::BadValue;
        }
        // The low bytes of a little-endian value are its first bytes (the reader asserts the host)
        const uint64_t wide = index;
        std::memcpy(address, &wide, at.type->enumBytes);
        return PanelReadStatus::Ok;
    }

    PanelReadStatus PanelBindingWriter::WriteText(const PanelDataSources& sources, const PanelBinding& binding, StringView text) {
        const PanelBindingReader::Located at = _reader.LocateBinding(sources, binding);
        return at.status != PanelReadStatus::Ok ? at.status : Assign(at, text);
    }

    PanelReadStatus PanelBindingWriter::WriteRowText(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, StringView text) {
        PanelReadStatus status = PanelReadStatus::Ok;
        const PanelBindingReader::Located at = _reader.LocateCell(sources, list, row, relativePath, status);
        return status != PanelReadStatus::Ok ? status : Assign(at, text);
    }

    PanelReadStatus PanelBindingWriter::WriteBool(const PanelDataSources& sources, const PanelBinding& binding, bool value) {
        const PanelBindingReader::Located at = _reader.LocateBinding(sources, binding);
        return at.status != PanelReadStatus::Ok ? at.status : AssignBool(at, value);
    }

    PanelReadStatus PanelBindingWriter::WriteNumber(const PanelDataSources& sources, const PanelBinding& binding, double value) {
        const PanelBindingReader::Located at = _reader.LocateBinding(sources, binding);
        return at.status != PanelReadStatus::Ok ? at.status : AssignNumber(at, value);
    }

    PanelReadStatus PanelBindingWriter::WriteEnumIndex(const PanelDataSources& sources, const PanelBinding& binding, size_t index) {
        const PanelBindingReader::Located at = _reader.LocateBinding(sources, binding);
        return at.status != PanelReadStatus::Ok ? at.status : AssignEnumIndex(at, index);
    }

    PanelReadStatus PanelBindingWriter::WriteRowBool(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, bool value) {
        PanelReadStatus status = PanelReadStatus::Ok;
        const PanelBindingReader::Located at = _reader.LocateCell(sources, list, row, relativePath, status);
        return status != PanelReadStatus::Ok ? status : AssignBool(at, value);
    }

    PanelReadStatus PanelBindingWriter::WriteRowNumber(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, double value) {
        PanelReadStatus status = PanelReadStatus::Ok;
        const PanelBindingReader::Located at = _reader.LocateCell(sources, list, row, relativePath, status);
        return status != PanelReadStatus::Ok ? status : AssignNumber(at, value);
    }

    PanelReadStatus PanelBindingWriter::WriteRowEnumIndex(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, size_t index) {
        PanelReadStatus status = PanelReadStatus::Ok;
        const PanelBindingReader::Located at = _reader.LocateCell(sources, list, row, relativePath, status);
        return status != PanelReadStatus::Ok ? status : AssignEnumIndex(at, index);
    }

}  // namespace BECore

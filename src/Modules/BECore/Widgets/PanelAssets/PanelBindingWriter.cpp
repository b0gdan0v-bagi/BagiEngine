#include "PanelBindingWriter.h"

namespace BECore {

    PanelReadStatus PanelBindingWriter::Assign(const PanelBindingReader::Located& at, StringView text) {
        if (at.address == nullptr || at.type->kind != TypeKind::String || !at.type->isEngineString || !at.writable) {
            return PanelReadStatus::WrongType;
        }
        // The object is not const (checked above), and the field is a String at this address
        String& field = *reinterpret_cast<String*>(const_cast<uint8_t*>(at.address));
        field.assign(text.data(), text.size());
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

}  // namespace BECore

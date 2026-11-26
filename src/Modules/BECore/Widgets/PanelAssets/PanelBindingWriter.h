#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief Writes what a control of a panel changed back into the field a binding names: a text, a bool, a number or an enum
     *
     * It finds the field through the reader it was given, so the compiled paths are one cache and a steady frame allocates nothing when
     * the new text fits the field's capacity. Only a field held by value (at the top level, inside embedded objects and in list rows) is
     * written, and only by the call of its kind: `WriteText` a `String`, `WriteBool` a bool, `WriteNumber` an integer or a float (the
     * value is rounded for an integer and clamped to what the field's type holds, so a wide value never wraps; NaN and infinity are
     * `BadValue`), `WriteEnumIndex` an enum by the index of its name (an index past the names is `BadValue`). Any other kind of field is
     * `WrongType`: a `PoolString` is never interned here because an asset is untrusted, and neither a `FixedString`, a field behind an
     * optional or a pointer, nor a field of a const source object is written. A refused write changes nothing. A wrong path, source or
     * row is a status as in the reader, never an assert or a crash. A binding's `!` means nothing to a write. Main thread only.
     */
    class PanelBindingWriter {
    public:
        /// @p reader must outlive the writer
        explicit PanelBindingWriter(PanelBindingReader& reader) : _reader(reader) {}

        [[nodiscard]] PanelReadStatus WriteText(const PanelDataSources& sources, const PanelBinding& binding, StringView text);

        /// The cell of row @p row at @p relativePath (empty for a list of `String`)
        [[nodiscard]] PanelReadStatus WriteRowText(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, StringView text);

        [[nodiscard]] PanelReadStatus WriteBool(const PanelDataSources& sources, const PanelBinding& binding, bool value);
        [[nodiscard]] PanelReadStatus WriteNumber(const PanelDataSources& sources, const PanelBinding& binding, double value);
        [[nodiscard]] PanelReadStatus WriteEnumIndex(const PanelDataSources& sources, const PanelBinding& binding, size_t index);

        [[nodiscard]] PanelReadStatus WriteRowBool(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, bool value);
        [[nodiscard]] PanelReadStatus WriteRowNumber(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, double value);
        [[nodiscard]] PanelReadStatus WriteRowEnumIndex(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, size_t index);

    private:
        /// The place a write may change, or null: no value held by value there, or the object it sits in is const
        static uint8_t* WritableAddress(const PanelBindingReader::Located& at);
        static PanelReadStatus Assign(const PanelBindingReader::Located& at, StringView text);
        static PanelReadStatus AssignBool(const PanelBindingReader::Located& at, bool value);
        static PanelReadStatus AssignNumber(const PanelBindingReader::Located& at, double value);
        static PanelReadStatus AssignEnumIndex(const PanelBindingReader::Located& at, size_t index);

        PanelBindingReader& _reader;
    };

}  // namespace BECore

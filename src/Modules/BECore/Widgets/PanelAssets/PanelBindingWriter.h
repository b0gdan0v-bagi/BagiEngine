#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <cstddef>

namespace BECore {

    /**
     * @brief Writes a text typed into a panel back into the `String` field a binding names
     *
     * It finds the field through the reader it was given, so the compiled paths are one cache and a steady frame allocates nothing when
     * the new text fits the field's capacity. Only a `String` held by value (at the top level, inside embedded objects and in list rows) is
     * written: a `PoolString` is never interned here because an asset is untrusted, and a number, a `FixedString`, a field behind an
     * optional or a pointer, or a field of a const source object is `WrongType`. A wrong path, source or row is a status as in the reader,
     * never an assert or a crash. Main thread only.
     */
    class PanelBindingWriter {
    public:
        /// @p reader must outlive the writer
        explicit PanelBindingWriter(PanelBindingReader& reader) : _reader(reader) {}

        [[nodiscard]] PanelReadStatus WriteText(const PanelDataSources& sources, const PanelBinding& binding, StringView text);

        /// The cell of row @p row at @p relativePath (empty for a list of `String`)
        [[nodiscard]] PanelReadStatus WriteRowText(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, StringView text);

    private:
        static PanelReadStatus Assign(const PanelBindingReader::Located& at, StringView text);

        PanelBindingReader& _reader;
    };

}  // namespace BECore

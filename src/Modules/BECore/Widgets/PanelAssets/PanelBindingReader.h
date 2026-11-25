#pragma once

#include <BECore/Reflection/CompiledPath.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Why a read gave no value: the panel shows a `?` for any of them and never stops
    CORE_ENUM(PanelReadStatus, uint8_t,
              Ok,             // the value is in the out parameter
              UnknownSource,  // no source of that name is registered
              NoObject,       // the source is registered but gives no object now (the active game is gone)
              BadPath,        // the object's type has no such field
              NoValue,        // the path is right but leads nowhere now: an empty optional, a position past the end of a list inside the path
              WrongType,      // the field is not of the kind the call reads
              NotAList,       // ListCount and the row reads need a list at a fixed path
              NoRow,          // the row is past the end of the list
              BadValue)       // a write of a value the field cannot hold: NaN or infinity, an enum index past the names; a read of an enum that holds none of its names

    /**
     * @brief Reads what a panel shows from a bound field: a bool, a number, a text, an enum's index and names, the rows of a list and a cell of a row
     *
     * Every call takes the registry and the binding, asks the source for its object **now** and never keeps what it got. A wrong path, type
     * or source is a status, never an assert or a crash. A path is compiled against the object's type once and kept (`CompilePath`, keyed by the
     * `TypeInfo*` and the text), so the active game's type changing at run time is a miss that compiles again, and a path that does not
     * compile is remembered too, so a wrong binding costs no allocation in a steady frame.
     *
     * **Allocation:** a field held by value (a bool, a number, an enum, a `String` or a `PoolString`, also inside embedded objects and list
     * rows) is read in place and allocates nothing but the first compile of its path, and a text only into `scratch` when it outgrows it.
     * A path that needs the live object (an element of a list, an optional, a pointer inside the path) and a text field of another type
     * (`FixedString`) go through `GetProperty`, which allocates: bind those rarely.
     *
     * A list must sit at a fixed path of the source (a field of the source object or of an embedded object); its elements are a reflected
     * struct (a cell is a path relative to the row) or a scalar (the cell is the row itself, the relative path is empty). Main thread only.
     */
    class PanelBindingReader {
    public:
        /// The condition of a binding: the value of a bool field, inverted for `!source.path`
        [[nodiscard]] PanelReadStatus ReadBool(const PanelDataSources& sources, const PanelBinding& binding, bool& out);

        /// Any number field (an integer or a float); a bool, an enum and a text are `WrongType`. @p isInteger, when given, tells an integer field from a float one
        [[nodiscard]] PanelReadStatus ReadNumber(const PanelDataSources& sources, const PanelBinding& binding, double& out, bool* isInteger = nullptr);

        /// A text, a number, a bool or an enum name as text. @p out views the field itself or @p scratch: valid until either changes
        [[nodiscard]] PanelReadStatus ReadText(const PanelDataSources& sources, const PanelBinding& binding, String& scratch, StringView& out);

        /// The index of the name an enum field holds (in `EnumNames` order); `BadValue` when the stored value is none of them, `WrongType` for a field that is not an enum. The path must be fixed: no
        /// list position, optional or pointer in it (`NoValue`)
        [[nodiscard]] PanelReadStatus ReadEnumIndex(const PanelDataSources& sources, const PanelBinding& binding, size_t& out);

        /// The names of the enum the field has, in declaration order. @p out views static type data: valid for the run, nothing is allocated
        [[nodiscard]] PanelReadStatus EnumNames(const PanelDataSources& sources, const PanelBinding& binding, Span<const StringView>& out);

        /// The number of rows of the list the binding names
        [[nodiscard]] PanelReadStatus ListCount(const PanelDataSources& sources, const PanelBinding& list, size_t& count);

        /// The cell of row @p row at @p relativePath (relative to the row; empty for a list of scalars), as ReadText reads a field
        [[nodiscard]] PanelReadStatus ReadRowText(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, String& scratch, StringView& out);

        /// The cell of a row as ReadBool reads a field (no negation: a cell has no condition syntax)
        [[nodiscard]] PanelReadStatus ReadRowBool(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, bool& out);

        /// Forgets the compiled paths (a test; the cache also clears itself at `kMaxCompiledPaths`)
        void Clear();

        /// The compiled paths held, those that did not compile too
        [[nodiscard]] size_t CompiledCount() const {
            return _compiledCount;
        }

        /// A bound path comes from an asset file; the cache must not grow with every reload of a changing file
        static constexpr size_t kMaxCompiledPaths = 4096;

    private:
        friend class PanelBindingWriter;

        struct Compiled {
            bool valid = false;
            CompiledPath path;
        };

        /// A value found: the object it was read from, its compiled path and, for a path held by value, where it is
        struct Located {
            PanelReadStatus status = PanelReadStatus::NoObject;
            TypedRef object;
            const CompiledPath* path = nullptr;
            const uint8_t* address = nullptr;
            const TypeDesc* type = nullptr;
            /// The object the value sits in is not const: only the writer looks at it
            bool writable = false;
        };

        struct ListView {
            const uint8_t* data = nullptr;
            size_t count = 0;
            size_t stride = 0;
            const TypeDesc* element = nullptr;
            bool writable = false;
        };

        const Compiled* Compile(const TypeInfo& type, StringView path);
        Located Locate(TypedRef object, StringView path);
        Located LocateBinding(const PanelDataSources& sources, const PanelBinding& binding);
        PanelReadStatus FindList(const PanelDataSources& sources, const PanelBinding& list, ListView& out);
        Located LocateCell(const PanelDataSources& sources, const PanelBinding& list, size_t row, StringView relativePath, PanelReadStatus& status);

        static PanelReadStatus AsBool(const Located& at, bool& out);
        static PanelReadStatus AsNumber(const Located& at, double& out, bool* isInteger = nullptr);
        static PanelReadStatus AsText(const Located& at, String& scratch, StringView& out);
        static PanelReadStatus AsEnumIndex(const Located& at, size_t& out);

        HashMap<const TypeInfo*, HashMap<String, Compiled>> _compiled;
        size_t _compiledCount = 0;
    };

}  // namespace BECore

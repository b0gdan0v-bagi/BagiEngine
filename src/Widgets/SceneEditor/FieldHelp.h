#pragma once

#include <BECore/Config/FieldHelpConfig.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <expected>

namespace BECore {

    class FileSystem;
    struct FieldDesc;
    struct TypeInfo;

    /// One of the two help files: `config/FieldHelpConfig.xml` and `config/GameFieldHelpConfig.xml`
    CORE_ENUM(FieldHelpFile, uint8_t, Engine, Game)

    /// What answered a description: a help file, the field's own `tooltip` attribute or nothing
    CORE_ENUM(FieldHelpSource, uint8_t, None, Engine, Game, Attribute)

    struct FieldHelpAnswer {
        /// Empty exactly when `source` is None; valid until the next change or reload of the table
        StringView text;
        FieldHelpSource source = FieldHelpSource::None;
    };

    /// The reflected field of this serialized name in @p type or in its nearest ancestor that declares one; null when none does. Allocates nothing
    const FieldDesc* FindReflectedField(const TypeInfo& type, StringView name);

    /**
     * @brief The descriptions of the component fields: the two help files, read through a file system, with the field's attribute behind them
     *
     * The table is empty until `Reload`. It reads the files itself and not through `ConfigManager`, so a file made while the program runs is found.
     * A missing file is no error; a file that cannot be read is one warning, gives no entry, and `Save` leaves it as it is.
     */
    class FieldHelp {
    public:
        /// Over `CoreManager`'s file system
        FieldHelp() = default;
        /// Over a file system of the caller, which must outlive the table
        explicit FieldHelp(const FileSystem& fileSystem) : _fileSystem(&fileSystem) {}

        /// Whether a config of this name is one of the two help files, so a change of it asks for a `Reload`
        static bool IsHelpConfig(PoolString name);

        /// Reads both files again; changes that were not saved are lost
        void Reload();

        /**
         * @brief The description of a field of the component class @p component
         *
         * The classes are asked from @p component up its reflected parents, so an entry of a class wins over one of a parent; for one class the game's
         * entry wins over the engine's. With no entry the field's `tooltip` attribute answers. An entry with no text is none.
         */
        [[nodiscard]] FieldHelpAnswer Find(StringView component, StringView field) const;

        /// Adds or replaces the entry of @p file for exactly this class and field; nothing is written until `Save`
        void Set(FieldHelpFile file, StringView component, StringView field, StringView text);

        /// False when @p file has no entry for exactly this class and field
        bool Remove(FieldHelpFile file, StringView component, StringView field);

        /**
         * @brief Writes the whole file, its entries sorted by class and field (comments of the file are lost), where `FileSystem::WriteTarget` puts it
         *
         * A file that is in no layer yet is made in the top layer of `config` (the game's) or, for the engine's, in the bottom one.
         * A file that cannot be read, now or at the last `Reload`, is not written: the error says why.
         */
        [[nodiscard]] std::expected<void, String> Save(FieldHelpFile file);

        /// Moves on every change of an entry and every `Reload`: what a user composed from the table is stale when it does
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

    private:
        struct Table {
            /// Sorted by class and field, one per pair
            Vector<FieldHelpEntry> entries;
            bool unreadable = false;
        };

        [[nodiscard]] const FileSystem& Files() const;
        [[nodiscard]] Table& TableOf(FieldHelpFile file) {
            return _tables[static_cast<size_t>(file)];
        }
        [[nodiscard]] const FieldHelpEntry* FindEntry(FieldHelpFile file, StringView component, StringView field) const;

        Array<Table, 2> _tables;
        const FileSystem* _fileSystem = nullptr;
        uint64_t _version = 0;
    };

}  // namespace BECore

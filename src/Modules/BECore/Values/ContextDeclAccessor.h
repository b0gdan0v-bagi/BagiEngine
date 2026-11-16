#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Values/Context.h>

namespace BECore::Ser {

    /**
     * @brief A `ContextDecl` is the `<context>` element of a document: the levels its values may read, each a name and a registered type
     *
     * Every format reads the list form `<context><level name="caster" type="UnitStats"/></context>`; the text formats also read the
     * shorthand `<context caster="UnitStats"/>` (every attribute is a level, before the `level` children). `Save` writes the list form, and
     * nothing for a declaration with no level. A load that succeeds gives the declaration to the innermost `ValueLinkScope`
     * (`ValueLinkScope::Declare`), which keeps a copy for the `from` of the values read after it, so a document type declares the
     * context in a field placed **before** its `Value` fields. Any refusal is reported under the path of the level and installs
     * nothing: the field and the scope stay as they were.
     */
    template <>
    struct DataAccessor<ContextDecl> {
        static void Save(ISerializer& s, const ContextDecl& declaration, StringView key);
        static bool Load(IDeserializer& d, ContextDecl& declaration, StringView key);
    };

}  // namespace BECore::Ser

#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Values/Effects/EffectScript.h>

namespace BECore::Ser {

    namespace Detail {

        inline constexpr StringView kEffectScriptKey = "script";

        /// The `script` text of an open object scope compiled against the declaration and the write targets of the scope of the load into @p script, which stays as it was on an error
        bool LoadEffectScript(IDeserializer& d, EffectScript& script);

    }  // namespace Detail

    /**
     * @brief An `EffectScript` is `<onTick script="cell.timer -= 1; ..."/>`: the object form of a formula with the key `script` (`Effects/CLAUDE.md`, "In content")
     *
     * Compiled while the document loads, against the declaration and the `UseWriteTargets` of the `ValueLinkScope` of the load; a missing `script`, a `from`, `expr` or `value` beside it, no
     * declaration, no targets and any compile error are load errors under `script` (`column N: <message>`, ending with the document's name). A load that fails leaves the field as it was.
     * `script=""` is a script with no statements. `Save` writes the text as it was loaded.
     */
    template <>
    struct DataAccessor<EffectScript> {
        static void Save(ISerializer& s, const EffectScript& script, StringView key) {
            if (s.BeginObject(key)) {
                s.WriteAttribute(Detail::kEffectScriptKey, script.Source());
                s.EndObject();
            }
        }

        static bool Load(IDeserializer& d, EffectScript& script, StringView key) {
            if (!d.BeginObject(key)) {
                return false;
            }
            const bool loaded = Detail::LoadEffectScript(d, script);
            d.EndObject();
            return loaded;
        }
    };

}  // namespace BECore::Ser

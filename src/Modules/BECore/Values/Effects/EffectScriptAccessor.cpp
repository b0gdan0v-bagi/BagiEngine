#include "EffectScriptAccessor.h"

#include <BECore/Values/Effects/EffectCompiler.h>
#include <BECore/Values/Effects/WriteTargets.h>
#include <BECore/Values/ValueAccessor.h>
#include <BECore/Values/ValueLinkScope.h>

namespace BECore::Ser::Detail {

    bool LoadEffectScript(IDeserializer& d, EffectScript& script) {
        // Asked first and always, so the keys of a value are known to the strict mode and a script beside one of them is told
        for (const StringView other : {kValueFromKey, kValueExprKey, kValueLiteralKey}) {
            if (d.HasKey(other)) {
                d.ReportError(other, Format("A script is the text of 'script' only, and '{}' is there beside it", other));
                return false;
            }
        }
        String text;
        if (!d.ReadAttribute(kEffectScriptKey, text)) {
            // A key that is there and failed to read was reported by the reader
            if (!d.HasKey(kEffectScriptKey)) {
                d.ReportError(kEffectScriptKey, "A script needs its statements in 'script'");
            }
            return false;
        }
        const ContextDecl* declaration = ValueLinkScope::Current();
        if (declaration == nullptr) {
            d.ReportError(kEffectScriptKey, ValueLinkScope::AtDocument("No context is declared where this script is read, so it names no field"));
            return false;
        }
        const WriteTargets* targets = ValueLinkScope::Targets();
        if (targets == nullptr) {
            d.ReportError(kEffectScriptKey, ValueLinkScope::AtDocument("The host declared no write targets where this script is read (ValueLinkScope::UseWriteTargets)"));
            return false;
        }
        EffectOptions options;
        options.targets = targets;
        options.tables = ValueLinkScope::Tables();
        const auto compiled = CompileEffects(text, *declaration, options);
        if (!compiled.has_value()) {
            d.ReportError(kEffectScriptKey, ValueLinkScope::AtDocumentWithin(DescribeProgramError(compiled.error())));
            return false;
        }
        script = *compiled;
        return true;
    }

}  // namespace BECore::Ser::Detail

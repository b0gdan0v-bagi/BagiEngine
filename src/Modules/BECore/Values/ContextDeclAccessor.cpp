#include "ContextDeclAccessor.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/ValueLinkScope.h>

namespace BECore::Ser {

    namespace {

        constexpr StringView kLevelElement = "level";
        constexpr StringView kLevelNameKey = "name";
        constexpr StringView kLevelTypeKey = "type";

        /// Adds one level to @p loaded; the refusal is reported under @p nameKey or @p typeKey, whichever it is about
        bool AddLevel(IDeserializer& d, ContextDecl& loaded, StringView name, StringView type, StringView nameKey, StringView typeKey) {
            if (name.empty()) {
                d.ReportError(nameKey, "a context level needs a name");
                return false;
            }
            if (type.empty()) {
                d.ReportError(typeKey, Format("the level '{}' needs a type", name));
                return false;
            }
            const auto added = loaded.Add(name, type);
            if (!added.has_value()) {
                d.ReportError(added.error().code == LinkErrorCode::unknown_type ? typeKey : nameKey, added.error().message);
                return false;
            }
            return true;
        }

    }  // namespace

    void DataAccessor<ContextDecl>::Save(ISerializer& s, const ContextDecl& declaration, StringView key) {
        if (declaration.Size() == 0) {
            return;
        }
        size_t count = declaration.Size();
        if (!s.BeginArray(key, kLevelElement, count)) {
            return;
        }
        for (const ContextLevel& level : declaration.Levels()) {
            if (s.BeginArrayElement()) {
                s.WriteAttribute(kLevelNameKey, StringView(level.name));
                s.WriteAttribute(kLevelTypeKey, level.type->Name());
                s.EndArrayElement();
            }
        }
        s.EndArray();
    }

    bool DataAccessor<ContextDecl>::Load(IDeserializer& d, ContextDecl& declaration, StringView key) {
        size_t count = 0;
        if (!d.BeginArray(key, kLevelElement, count)) {
            return false;
        }
        ContextDecl loaded;
        bool complete = true;

        // The shorthand: every attribute of the element is a level. Binary and JSON have none, so they read the list only
        Vector<String> attributes;
        d.AttributeNames(attributes);
        for (const String& attribute : attributes) {
            String type;
            if (!d.ReadAttribute(attribute, type)) {
                complete = false;
                continue;
            }
            complete = AddLevel(d, loaded, attribute, type, attribute, attribute) && complete;
        }

        for (size_t i = 0; i < count; ++i) {
            if (!d.BeginArrayElement()) {
                continue;
            }
            String name;
            String type;
            d.ReadAttribute(kLevelNameKey, name);
            d.ReadAttribute(kLevelTypeKey, type);
            complete = AddLevel(d, loaded, name, type, kLevelNameKey, kLevelTypeKey) && complete;
            d.EndArrayElement();
        }
        d.EndArray();
        if (!complete) {
            return false;
        }

        if (const auto declared = ValueLinkScope::Declare(loaded); !declared.has_value()) {
            d.ReportError(key, declared.error());
            return false;
        }
        declaration = loaded;
        return true;
    }

}  // namespace BECore::Ser

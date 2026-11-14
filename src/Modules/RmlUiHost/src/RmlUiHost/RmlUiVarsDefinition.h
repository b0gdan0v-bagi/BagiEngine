#pragma once

#include <RmlUi/Core/DataVariable.h>

namespace BECore {

    /**
     * @brief UIVars seen by an RmlUi data model as the struct `vars`: `{{vars.score}}` in a document shows the variable `score` that game code set.
     *
     * RmlUi binds text to its own data models, so this is the adapter that lets a game keep calling `CoreManager::GetUIVars().Set(...)` for every authoring path. It reads
     * the table at the moment RmlUi asks, so the owner only has to mark the variable dirty (`DirtyVariable("vars")`) when `UIVars::GetVersion()` changed.
     */
    class RmlUiVarsDefinition final : public Rml::VariableDefinition {
    public:
        RmlUiVarsDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Struct) {}

        Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override;

        /// The variable to hand to DataModelConstructor::BindCustomDataVariable (its pointer is this object's, never read)
        [[nodiscard]] Rml::DataVariable GetVariable() {
            return Rml::DataVariable(this, this);
        }

    private:
        class Scalar final : public Rml::VariableDefinition {
        public:
            Scalar() : Rml::VariableDefinition(Rml::DataVariableType::Scalar) {}
            bool Get(void* ptr, Rml::Variant& variant) override;
        };

        Scalar _scalar;
        eastl::hash_map<String, PoolString> _names;  // the node of a hash map keeps its address, which is the pointer RmlUi holds for the variable
    };

}  // namespace BECore

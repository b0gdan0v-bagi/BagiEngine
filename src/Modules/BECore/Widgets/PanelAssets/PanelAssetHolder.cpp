#include "PanelAssetHolder.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Values/ValueLinkScope.h>

namespace BECore {

    namespace {

        constexpr StringView kFileWhere = "file";
        constexpr StringView kConfigNotFound = "config not found";

    }  // namespace

    void PanelAssetHolder::Load(const ConfigManager& configs, StringView name, const PanelValidationContext& context) {
        PanelLoadResult result;
        {
            const ValueLinkScope scope(name);
            const UniquePtr<IDeserializer> deserializer = configs.CreateDeserializer(name);
            if (deserializer == nullptr) {
                result.issues.push_back({String(kFileWhere), String(kConfigNotFound)});
            } else {
                result = LoadPanelAsset(*deserializer, context);
            }
        }
        ApplyResult(std::move(result));
    }

    void PanelAssetHolder::ApplyResult(PanelLoadResult result) {
        _failure.clear();
        _issues = std::move(result.issues);
        if (_issues.empty() && result.asset != nullptr) {
            _current = std::move(result.asset);
            ++_version;
        }
    }

    void PanelAssetHolder::ApplyReloadFailure(StringView reason) {
        _failure = String(reason);
    }

}  // namespace BECore

#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelAsset.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <cstdint>

namespace BECore {

    class ConfigManager;

    /**
     * @brief The panel asset a host draws and the rule that a bad file never throws away a good one
     *
     * `Current()` is the last asset that was read with no issue; a read that has issues keeps it and only sets `Issues()`, so a host
     * draws the banner and the last good asset below it. A first read with issues leaves `Current()` null: the host has the banner alone.
     * `Failure()` is the reason of a reload that could not read the file at all (`ConfigReloadFailedEvent`); a later read, good or not,
     * replaces that news with its own result. Main thread only, like the interpreter that draws `Current()`.
     */
    class PanelAssetHolder {
    public:
        /**
         * @brief Reads the config @p name and applies the result
         *
         * The read runs under a `ValueLinkScope(name)` and the deserializer is gone before the call returns, so it never outlives a
         * reload of the config. A config the manager does not know is the issue "config not found".
         */
        void Load(const ConfigManager& configs, StringView name, const PanelValidationContext& context);

        void ApplyResult(PanelLoadResult result);

        void ApplyReloadFailure(StringView reason);

        /// The last asset read with no issue, or null
        [[nodiscard]] const IntrusivePtr<PanelAsset>& Current() const {
            return _current;
        }

        /// The issues of the last read; empty after a good one
        [[nodiscard]] const Vector<PanelIssue>& Issues() const {
            return _issues;
        }

        /// The reason of the last failed reload; empty when none failed since the last read
        [[nodiscard]] const String& Failure() const {
            return _failure;
        }

        /// Changes when `Current()` is replaced, so a host prepares its drawing of the asset again only then
        [[nodiscard]] uint32_t Version() const {
            return _version;
        }

    private:
        IntrusivePtr<PanelAsset> _current;
        Vector<PanelIssue> _issues;
        String _failure;
        uint32_t _version = 0;
    };

}  // namespace BECore

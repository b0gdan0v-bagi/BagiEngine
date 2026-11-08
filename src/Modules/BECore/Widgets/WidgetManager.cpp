#include "WidgetManager.h"

#include <BECore/Application/RunMode.h>
#include <BECore/Config/ConfigFactory.h>
#include <BECore/Widgets/WidgetList.h>

namespace BECore {

    void WidgetManager::RegisterWidget(IntrusivePtr<IWidget> widget) {
        if (widget) {
            _widgets.push_back(std::move(widget));
        }
    }

    void WidgetManager::UpdateAll() const {
        for (auto& widget : _widgets) {
            if (widget) {
                widget->Update();
            }
        }
    }

    void WidgetManager::DrawScreenAll() const {
        for (auto& widget : _widgets) {
            if (widget) {
                widget->DrawScreen();
            }
        }
    }

    void WidgetManager::DrawAll() const {
        for (auto& widget : _widgets) {
            if (widget) {
                widget->Draw();
            }
        }
    }

    namespace {

        /// The widgets of the config of @p mode, in file order
        eastl::vector<IntrusivePtr<IWidget>> LoadWidgetsFromConfig(RunMode mode) {
            eastl::vector<IntrusivePtr<IWidget>> widgets;
            const PoolString configName = GetWidgetsConfigName(mode);
            if (!CoreManager::GetConfigManager().HasConfig(configName)) {
                LOG_ERROR("WidgetManager: config '{}' not found, no widgets created"_format(configName.ToStringView()));
                return widgets;
            }

            // Unknown types and widgets whose Initialize returns false are reported by MakeFromConfig and left out of the list
            const auto list = MakeFromConfig<WidgetList>(configName.ToStringView());
            for (const auto& widget : list->widgets) {
                widgets.push_back(widget);
            }
            return widgets;
        }

    }  // namespace

    void WidgetManager::CreateWidgets() {
        const RunMode mode = ResolveRunMode(ReadRunMode(CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern)));
        LOG_INFO("Run mode: {}"_format(EnumUtils<RunMode>::ToString(mode)));
        CreateWidgets(mode);
    }

    void WidgetManager::CreateWidgets(RunMode mode) {
        _mode = mode;
        auto widgets = _loader ? _loader(mode) : LoadWidgetsFromConfig(mode);
        for (auto& widget : widgets) {
            RegisterWidget(std::move(widget));
        }
    }

#if BE_WITH_EDITOR
    void WidgetManager::RequestMode(RunMode mode) {
        if (mode == _mode) {
            _pendingMode.reset();
        } else {
            _pendingMode = mode;
        }
    }

    void WidgetManager::RequestToggle() {
        RequestMode(OtherRunMode(_pendingMode.value_or(_mode)));
    }

    bool WidgetManager::ApplyPendingMode() {
        if (!_pendingMode) {
            return false;
        }
        const RunMode mode = *_pendingMode;
        _pendingMode.reset();

        if (const auto& renderer = CoreManager::GetRenderer()) {
            renderer->WaitIdle();
        }
        _widgets.clear();
        CreateWidgets(mode);
        LOG_INFO("Run mode: {}"_format(EnumUtils<RunMode>::ToString(mode)));
        return true;
    }
#endif
}  // namespace BECore

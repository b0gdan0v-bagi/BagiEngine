#pragma once

namespace BECore {

    /**
     * @brief How the executable runs, chosen by the `mode` attribute of config/ApplicationConfig.xml.
     *
     * Editor: ImGui, the editor panels and the scene shown in the viewport window (config/WidgetsConfig.xml).
     * Game: no ImGui; the scene is drawn straight into the back buffer (config/GameWidgetsConfig.xml).
     */
    CORE_ENUM(RunMode, uint8_t, Editor, Game)

    /// Reads the `mode` attribute of the ApplicationConfig root. A missing or unknown value gives RunMode::Editor.
    RunMode ReadRunMode(const XmlNode& applicationConfig);

    /// Name of the config that lists the widgets created in @p mode.
    PoolString GetWidgetsConfigName(RunMode mode);

}  // namespace BECore

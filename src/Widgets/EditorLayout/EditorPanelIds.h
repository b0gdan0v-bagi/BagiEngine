#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>

namespace BECore {

    /// The ids under which the engine's docked panels register: the asset's id for a panel asset, the widget's own for a panel written in C++
    namespace EditorPanelIds {
        inline constexpr auto kViewport = "Viewport"_intern;
        inline constexpr auto kSceneEditor = "SceneEditor"_intern;
        inline constexpr auto kConfigEditor = "ConfigEditor"_intern;
        inline constexpr auto kSpriteLibrary = "SpriteLibrary"_intern;
        inline constexpr auto kAssetBrowser = "AssetBrowser"_intern;
        inline constexpr auto kConsole = "Console"_intern;
        inline constexpr auto kProfiler = "Profiler"_intern;
        inline constexpr auto kGenerate = "Generate"_intern;
        inline constexpr auto kGamePanel = "GamePanel"_intern;
        inline constexpr auto kFileChanges = "FileChanges"_intern;
        inline constexpr auto kFieldHelp = "FieldHelp"_intern;
        inline constexpr auto kActivity = "Activity"_intern;
    }  // namespace EditorPanelIds

}  // namespace BECore

#pragma once

#include <BECore/Application/RunMode.h>
#include <BECore/Control/ControlSettings.h>
#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    /// The names of the four attributes of ApplicationConfig that are control settings (the field names without the `_`);
    /// ResolveControlSettings tells their errors from those of `mode` and `type` by these.
    namespace ApplicationConfigFields {
        inline constexpr StringView kControl = "control";
        inline constexpr StringView kControlPort = "controlPort";
        inline constexpr StringView kManualClock = "manualClock";
        inline constexpr StringView kHeadless = "headless";
    }  // namespace ApplicationConfigFields

    /// The value of `taskFrameBudgetMs` that leaves the default frame budget of TaskManager as it is
    inline constexpr double kNoTaskFrameBudget = -1.0;

    /**
     * @brief The root attributes of config/ApplicationConfig.xml, read through the reflection.
     *
     * `type`, `renderer` and `path` say which ApplicationFabric starts, which renderer it creates and where its window config is;
     * `mode` asks for the run mode (RunMode.h); `control`, `controlPort`, `manualClock` and `headless` are the settings of the control
     * API (ControlSettings.h, the command line is applied over them); `taskFrameBudgetMs` is the time per frame, in milliseconds, that a
     * single-thread build gives to the background tasks (TaskManager::SetFrameBudget; a negative or missing value keeps the default of
     * TaskManager). Every field has the default of a missing attribute.
     */
    struct ApplicationConfig {
        BE_CLASS(ApplicationConfig)

        BE_REFLECT_FIELD eastl::string _type;
        BE_REFLECT_FIELD eastl::string _renderer;
        BE_REFLECT_FIELD eastl::string _path;
        BE_REFLECT_FIELD RunMode _mode = RunMode::Editor;
        BE_REFLECT_FIELD bool _control = false;
        BE_REFLECT_FIELD uint16_t _controlPort = kDefaultControlPort;
        BE_REFLECT_FIELD bool _manualClock = false;
        BE_REFLECT_FIELD bool _headless = false;
        BE_REFLECT_FIELD double _taskFrameBudgetMs = kNoTaskFrameBudget;
    };

    /// An ApplicationConfig and what went wrong while reading it. A field with an error keeps its default.
    struct ApplicationConfigLoad {
        ApplicationConfig config;
        eastl::vector<DeserializeError> errors;
        bool found = false;  // false when there is no config named ApplicationConfig; config then holds the defaults
    };

    /**
     * @brief Reads the ApplicationConfig from @p deserializer (positioned at the root of the config).
     *
     * Switches the deserializer to strict values (IDeserializer::SetStrictValues): a boolean that is not true / false / 1 / 0 and a
     * port that is not a number from 0 to 65535 are errors, not zero. Writes no log; the caller decides what to do with the errors.
     * `found` of the result is true.
     */
    ApplicationConfigLoad LoadApplicationConfig(IDeserializer& deserializer);

    /// The config named @p configName of the ConfigManager of the CoreManager. A missing config gives the defaults and `found == false`.
    /// Writes no log. (The name is a parameter so that a test can ask for a config that does not exist.)
    ApplicationConfigLoad LoadApplicationConfig(PoolString configName);

    /// The config named `ApplicationConfig`: the file of the running application.
    ApplicationConfigLoad LoadApplicationConfig();

}  // namespace BECore

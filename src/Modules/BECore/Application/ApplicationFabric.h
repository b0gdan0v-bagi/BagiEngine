#pragma once

#include <BECore/Application/ApplicationOptions.h>

namespace BECore {

    struct ApplicationConfig;

    class ApplicationFabric : public Singleton<ApplicationFabric> {
    public:
        /// Creates the IApplicationFabric named by `config._type` and lets it start the window, the renderer and the events.
        /// An empty or unknown type is logged and gives false.
        bool Create(const ApplicationConfig& config, const ApplicationOptions& options = {});

    private:
        ApplicationFabric() = default;
        friend class Singleton<ApplicationFabric>;
    };
}  // namespace BECore

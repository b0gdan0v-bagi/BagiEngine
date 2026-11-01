#pragma once

#include <BECore/Application/ApplicationOptions.h>

namespace BECore {

    class ApplicationFabric : public Singleton<ApplicationFabric> {
    public:
        bool Create(const ApplicationOptions& options = {});

    private:
        ApplicationFabric() = default;
        friend class Singleton<ApplicationFabric>;
    };
}  // namespace BECore

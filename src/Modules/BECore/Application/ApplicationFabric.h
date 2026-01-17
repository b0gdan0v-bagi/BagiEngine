#pragma once

#include <BECore/Utils/Singleton.h>

namespace BECore {

    class ApplicationFabric : public Singleton<ApplicationFabric> {
    public:
        bool Create();

    private:
        ApplicationFabric() = default;
        friend class Singleton<ApplicationFabric>;
    };
}  // namespace BECore


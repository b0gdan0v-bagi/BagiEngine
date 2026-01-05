#pragma once

#include <Core/Utils/Singleton.h>

namespace Core {

    class ApplicationFabric : public Singleton<ApplicationFabric> {
    public:
        bool Create();

    private:
        ApplicationFabric() = default;
        friend class Singleton<ApplicationFabric>;
    };
}  // namespace Core


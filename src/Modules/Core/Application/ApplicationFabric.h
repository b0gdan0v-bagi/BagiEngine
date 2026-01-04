#pragma once

#include <Core/Utils/Singleton.h>
#include <string_view>

namespace Core {

    class ApplicationFabric : public Singleton<ApplicationFabric> {
    public:
        bool Create();

    private:
        ApplicationFabric() = default;
        friend class Singleton<ApplicationFabric>;
    };
}  // namespace Core


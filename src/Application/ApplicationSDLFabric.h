#pragma once

#include <Application/ApplicationMainAccess.h>

namespace Core {
    class XmlConfig;

    class ApplicationSDLFabric : public ApplicationMainAccess {
    public:
        static bool Create(const XmlConfig& config);
    };
}  // namespace Core

#pragma once

namespace Core {
    class XmlConfig;

    class ApplicationSDLFabric {
    public:
        static bool Create(const XmlConfig& config);
    };
}  // namespace Core

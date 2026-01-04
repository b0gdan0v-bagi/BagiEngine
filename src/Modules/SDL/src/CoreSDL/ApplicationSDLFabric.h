#pragma once

#include <Core/MainWindow/MainWindowAccessor.h>
#include <Core/Application/ApplicationMainAccess.h>

namespace Core {
    class XmlConfig;

    class ApplicationSDLFabric : public ApplicationMainAccess, public MainWindowAccessor {
    public:
        static bool Create(const XmlConfig& config);
    };
}  // namespace Core


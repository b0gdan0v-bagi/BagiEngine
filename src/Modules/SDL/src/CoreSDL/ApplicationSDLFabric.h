#pragma once

#include <BECore/MainWindow/MainWindowAccessor.h>
#include <BECore/Application/ApplicationMainAccess.h>

namespace BECore {
    class XmlConfig;

    class ApplicationSDLFabric : public ApplicationMainAccess, public MainWindowAccessor {
    public:
        static bool Create(const XmlConfig& config);
    };
}  // namespace BECore


#pragma once

#include <BECore/Application/ApplicationMainAccess.h>
#include <BECore/Application/ApplicationOptions.h>
#include <BECore/MainWindow/MainWindowAccessor.h>

namespace BECore {

    class XmlNode;

    class IApplicationFabric : public RefCounted, public ApplicationMainAccess, public MainWindowAccessor {
        BE_CLASS(IApplicationFabric, FACTORY_BASE)
    public:
        IApplicationFabric() = default;
        ~IApplicationFabric() override = default;

        BE_FUNCTION virtual bool Create(const XmlNode& configNode, const ApplicationOptions& options) = 0;
    };

}  // namespace BECore

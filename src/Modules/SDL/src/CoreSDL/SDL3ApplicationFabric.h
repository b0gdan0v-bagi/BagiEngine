#pragma once

#include <BECore/Application/IApplicationFabric.h>

namespace BECore {

    class SDL3ApplicationFabric : public IApplicationFabric {
        BE_CLASS(SDL3ApplicationFabric)
    public:
        bool Create(const XmlNode& configNode) override;
    };

}  // namespace BECore

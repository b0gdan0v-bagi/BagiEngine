#include "IPanelModel.h"

#include <BECore/Reflection/AbstractFactory.h>
#include <Generated/IPanelModel.gen.hpp>

namespace BECore {

    TypedRef IPanelModel::GetObject() {
        return TypedRef::Of(*this);
    }

    bool IsKnownPanelModel(StringView name) {
        return AbstractFactory<IPanelModel>::GetInstance().FindMeta(name) != nullptr;
    }

    IntrusivePtr<IPanelModel> CreatePanelModel(StringView name) {
        return AbstractFactory<IPanelModel>::GetInstance().Create(name);
    }

}  // namespace BECore

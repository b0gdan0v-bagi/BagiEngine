#include "ValueLinkScope.h"

namespace BECore {

    namespace {

        constinit thread_local const ValueLinkScope* t_innermost = nullptr;

    }  // namespace

    ValueLinkScope::ValueLinkScope(const ContextDecl& declaration) : _declaration(&declaration), _previous(t_innermost) {
        t_innermost = this;
    }

    ValueLinkScope::~ValueLinkScope() {
        t_innermost = _previous;
    }

    const ContextDecl* ValueLinkScope::Current() {
        return t_innermost != nullptr ? t_innermost->_declaration : nullptr;
    }

}  // namespace BECore

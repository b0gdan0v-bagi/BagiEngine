#include "ValueLinkScope.h"

#include <BECore/Reflection/ReaderLimits.h>

namespace BECore {

    namespace {

        constinit thread_local ValueLinkScope* t_innermost = nullptr;

    }  // namespace

    ValueLinkScope::ValueLinkScope(const ContextDecl& declaration) : _external(&declaration), _previous(t_innermost) {
        t_innermost = this;
    }

    ValueLinkScope::ValueLinkScope(StringView documentName) : _documentName(documentName), _previous(t_innermost) {
        t_innermost = this;
    }

    ValueLinkScope::~ValueLinkScope() {
        t_innermost = _previous;
    }

    const ContextDecl* ValueLinkScope::Current() {
        for (const ValueLinkScope* scope = t_innermost; scope != nullptr; scope = scope->_previous) {
            if (const ContextDecl* declaration = scope->Declaration()) {
                return declaration;
            }
        }
        return nullptr;
    }

    const IValueTables* ValueLinkScope::Tables() {
        for (const ValueLinkScope* scope = t_innermost; scope != nullptr; scope = scope->_previous) {
            if (scope->_tables != nullptr) {
                return scope->_tables;
            }
        }
        return nullptr;
    }

    const WriteTargets* ValueLinkScope::Targets() {
        for (const ValueLinkScope* scope = t_innermost; scope != nullptr; scope = scope->_previous) {
            if (scope->_writeTargets != nullptr) {
                return scope->_writeTargets;
            }
        }
        return nullptr;
    }

    std::expected<void, String> ValueLinkScope::Declare(const ContextDecl& declaration) {
        if (t_innermost == nullptr) {
            return std::unexpected(String("<context> is read where no link scope is open: load the document through MakeFromConfig or open a ValueLinkScope"));
        }
        if (t_innermost->Declaration() != nullptr) {
            return std::unexpected(AtDocument("this document already declares a context"));
        }
        t_innermost->_owned = declaration;
        return {};
    }

    StringView ValueLinkScope::DocumentName() {
        for (const ValueLinkScope* scope = t_innermost; scope != nullptr; scope = scope->_previous) {
            if (!scope->_documentName.empty()) {
                return scope->_documentName;
            }
        }
        return {};
    }

    String ValueLinkScope::AtDocument(StringView message) {
        const StringView name = DocumentName();
        return name.empty() ? String(message) : Format("{} (document '{}')", message, name);
    }

    String ValueLinkScope::AtDocumentWithin(StringView message) {
        const StringView name = DocumentName();
        if (name.empty()) {
            return String(message);
        }
        const String suffix = Format(" (document '{}')", name);
        constexpr StringView kCut = "...";
        if (message.size() + suffix.size() <= ReaderLimits::kMaxErrorTextBytes || suffix.size() + kCut.size() >= ReaderLimits::kMaxErrorTextBytes) {
            return Format("{}{}", message, suffix);
        }
        const size_t room = ReaderLimits::kMaxErrorTextBytes - suffix.size() - kCut.size();
        return Format("{}{}{}", message.substr(0, room), kCut, suffix);
    }

}  // namespace BECore

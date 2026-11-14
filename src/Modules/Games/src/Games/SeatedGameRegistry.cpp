#include "SeatedGameRegistry.h"

namespace BECore {

    bool SeatedGameRegistry::Register(SeatedGameInfo info, SeatedGameFactory factory) {
        if (info.name.empty() || info.seatCount < 1 || !factory) {
            return false;
        }
        const eastl::string key = info.name;
        _games[key] = Entry{std::move(info), std::move(factory)};
        return true;
    }

    Optional<SeatedGameInfo> SeatedGameRegistry::Find(eastl::string_view name) const {
        const auto it = _games.find(eastl::string(name));
        if (it == _games.end()) {
            return std::nullopt;
        }
        return it->second.info;
    }

    eastl::unique_ptr<ISeatedGame> SeatedGameRegistry::Create(eastl::string_view name) const {
        const auto it = _games.find(eastl::string(name));
        if (it == _games.end()) {
            return nullptr;
        }
        return it->second.factory();
    }

    size_t SeatedGameRegistry::GetCount() const {
        return _games.size();
    }

}  // namespace BECore

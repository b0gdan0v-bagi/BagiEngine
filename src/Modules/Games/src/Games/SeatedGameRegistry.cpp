#include "SeatedGameRegistry.h"

namespace BECore {

    bool SeatedGameRegistry::Register(SeatedGameInfo info, SeatedGameFactory factory) {
        if (info.name.empty() || info.seatCount < 1 || !factory) {
            return false;
        }
        const String key = info.name;
        _games[key] = Entry{std::move(info), std::move(factory)};
        return true;
    }

    Optional<SeatedGameInfo> SeatedGameRegistry::Find(StringView name) const {
        const auto it = _games.find(String(name));
        if (it == _games.end()) {
            return std::nullopt;
        }
        return it->second.info;
    }

    UniquePtr<ISeatedGame> SeatedGameRegistry::Create(StringView name) const {
        const auto it = _games.find(String(name));
        if (it == _games.end()) {
            return nullptr;
        }
        return it->second.factory();
    }

    size_t SeatedGameRegistry::GetCount() const {
        return _games.size();
    }

}  // namespace BECore

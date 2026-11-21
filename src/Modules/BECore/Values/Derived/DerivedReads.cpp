#include "DerivedReads.h"

#include <BECore/Values/ContextMessages.h>

namespace BECore {

    std::expected<void, LinkError> DerivedReads::AddLevel(const ContextDecl& declaration, StringView name) {
        const Optional<LevelIndex> index = declaration.IndexOf(name);
        if (!index.has_value()) {
            return std::unexpected(LinkError{LinkErrorCode::unknown_level, Format("derived reads: {}", DescribeUnknownLevel(declaration, name))});
        }
        _mask |= LevelBit(*index);
        return {};
    }

    bool DerivedReads::AddSource(const uint64_t& counter) {
        ASSERT(_sources.size() < kMaxVersionSources, "DerivedReads::AddSource: more sources than kMaxVersionSources");
        if (_sources.size() >= kMaxVersionSources) {
            return false;
        }
        _sources.push_back(&counter);
        return true;
    }

}  // namespace BECore

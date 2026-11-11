#include "ProfileZone.h"

namespace BECore {

    ProfileZone::ProfileZone(const ProfileSite& site) noexcept : _site(site), _backend(Profiler::GetBackend()) {
        if (_backend != nullptr) {
            _token = _backend->BeginZone(site);
        }
    }

    ProfileZone::~ProfileZone() {
        if (_backend != nullptr) {
            _backend->EndZone(_token, _site, ZoneAllocs{});
        }
    }

}  // namespace BECore

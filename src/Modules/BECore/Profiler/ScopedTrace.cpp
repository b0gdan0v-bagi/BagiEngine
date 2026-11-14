#include "ScopedTrace.h"

#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/Profiler.h>

namespace BECore {

    ScopedTrace::ScopedTrace(StringView path) : _path(path.data(), path.size()), _previous(Profiler::GetPrimaryBackend()) {
        Profiler::GetChromeTrace().MarkTraceStart();
        Profiler::SetBackend(&Profiler::GetChromeTrace());
    }

    ScopedTrace::~ScopedTrace() {
        Write();
        Profiler::SetBackend(_previous);
    }

    bool ScopedTrace::Write() {
        const auto written = Profiler::GetChromeTrace().WriteFile(_path);
        if (!written) {
            _error = written.error();
            LOG_ERROR("Profiler: {}"_format(_error));
            return false;
        }
        _error.clear();
        return true;
    }

}  // namespace BECore

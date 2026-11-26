#pragma once

namespace BECore {

    class ControlService;
    class PoolStringMonitor;

    /**
     * @brief Registers `pool_strings`, the view of the control API into the pool `monitor` watches (CLAUDE.md,
     * "Control method"; CoreManager::InitializeCoreServices registers it over the process pool)
     *
     * `monitor` is not owned and outlives the handler. A call interns nothing, so a client cannot grow the pool.
     */
    void RegisterPoolStringsControl(ControlService& service, PoolStringMonitor& monitor);

}  // namespace BECore

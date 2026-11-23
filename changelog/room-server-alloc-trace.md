# Allocation profiler: the trace writer pollutes traces, library blocks are unnamed, no size histogram, no trace in the room server

- Version: 0.10.1
- Entry: room-server-alloc-trace
- Date: 2026-11-23
- Bump: patch

## Added
- The allocation profiler keeps a size histogram per zone: `zone_allocs` answers it as `sizes`, the Chrome trace as `otherData.allocSizes`, and `tools/perf/trace_summary.py` prints the names and the sizes.
- The allocations of SDL, stb and pugixml are counted under their own names in `zone_allocs` instead of as unnamed blocks.
- `BagiRoomServer` and `BagiRoomClientProbe` take `--trace` and `--profiler`, their network path has zones (`Room*`, `WS*`, `Client*`), the probe has a load mode (`--clients`, `--messages`), and `tools/rooms/load.py` drives it.
- The asset load benchmark reports its allocations by zone (`AssetLoadAllocBenchTest.DISABLED_LoadByZone`), with a scene that runs particles.

## Changed
- Writing a Chrome trace formats into a reused buffer and writes in chunks under its own zone ("Trace write"), so it no longer allocates per event or pollutes the traces it writes.

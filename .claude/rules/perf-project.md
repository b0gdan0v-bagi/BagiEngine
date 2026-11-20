---
paths:
  - "tools/perf/**/*"
  - "src/Modules/BECore/Profiler/**/*"
---

# The benchmarks of this repository

Read by the skills `perf` and `perf-investigate` and by the sentinel step of `todo-merge` (`.claude/skills/perf/SKILL.md`; the runner and the format of the set: `tools/perf/CLAUDE.md`). A game that carries this repository as the submodule `engine/` has a file of the same name with its own set (it can run `{engine}/tools/perf/headless_frames.py` on its own executable); the skills are the same text in both.

## Benchmark set

```json
{
  "area": "engine",
  "preset": "macos-release",
  "profilePreset": "macos-profile",
  "targets": ["BagiStress", "BagiEngineTests"],
  "repetitions": 5,
  "thresholdPercent": 3,
  "benchmarks": [
    {
      "name": "stress-sprites",
      "about": "BagiStress sprites without a window: 5000 spinning sprites, one IRenderer call each, 300 frames after 30 of warm-up. work_ms_p50 is about 0.8 ms (the scene update and the calls); a frame is about 63 ms because the software renderer draws the sprites in its present, so more sprites or frames only make the run longer (10000 sprites: 43 s a run). The number a 2D batcher moves is work_ms",
      "fast": false,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "sprites", "--count", "5000", "--frames", "300", "--warmup", "30"],
      "traceCommand": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "sprites", "--count", "5000", "--frames", "300", "--warmup", "30", "--trace", "{trace}"],
      "metrics": {
        "work_ms_p50": {"source": "json", "key": "workMsP50", "unit": "ms/frame", "better": "lower"},
        "work_ms_p99": {"source": "json", "key": "workMsP99", "unit": "ms/frame", "better": "lower", "gate": false},
        "cpu_ms_p50": {"source": "json", "key": "cpuMsP50", "unit": "ms/frame", "better": "lower", "gate": false},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false},
        "allocs_per_frame": {"source": "json", "key": "allocsPerFrame", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "stress-entities",
      "about": "BagiStress entities without a window: 30000 nodes with a transform and a tween each, nothing drawn, 3000 frames after 120 of warm-up. work_ms_p50 is about 1.7 ms (p99 2.3), a run about 6 s; 50000 nodes would be 3.3 ms and 12 s",
      "fast": true,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "entities", "--count", "30000", "--frames", "3000", "--warmup", "120"],
      "traceCommand": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "entities", "--count", "30000", "--frames", "3000", "--warmup", "120", "--trace", "{trace}"],
      "metrics": {
        "work_ms_p50": {"source": "json", "key": "workMsP50", "unit": "ms/frame", "better": "lower"},
        "work_ms_p99": {"source": "json", "key": "workMsP99", "unit": "ms/frame", "better": "lower", "gate": false},
        "cpu_ms_p50": {"source": "json", "key": "cpuMsP50", "unit": "ms/frame", "better": "lower", "gate": false},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false},
        "allocs_per_frame": {"source": "json", "key": "allocsPerFrame", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "stress-ui",
      "about": "BagiStress ui without a window: a canvas of 3000 text labels in English and Russian, 3000 frames after 120 of warm-up. work_ms_p50 is about 1.7 ms of a 2.5 ms frame (the rest is the present), a run about 8 s; 5000 labels would be 2.1 ms and 10 s",
      "fast": true,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "ui", "--count", "3000", "--frames", "3000", "--warmup", "120"],
      "traceCommand": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "ui", "--count", "3000", "--frames", "3000", "--warmup", "120", "--trace", "{trace}"],
      "metrics": {
        "work_ms_p50": {"source": "json", "key": "workMsP50", "unit": "ms/frame", "better": "lower"},
        "work_ms_p99": {"source": "json", "key": "workMsP99", "unit": "ms/frame", "better": "lower", "gate": false},
        "cpu_ms_p50": {"source": "json", "key": "cpuMsP50", "unit": "ms/frame", "better": "lower", "gate": false},
        "draw_calls": {"source": "json", "key": "drawCalls", "unit": "per frame", "better": "lower", "gate": false},
        "allocs_per_frame": {"source": "json", "key": "allocsPerFrame", "unit": "per frame", "better": "lower", "gate": false}
      }
    },
    {
      "name": "stress-sprites-ramp",
      "about": "BagiStress sprites ramp: the count at which the whole CPU frame fits 16.6 ms (measure cpu: headless the software renderer draws the sprites inside its present, which work_ms leaves out, so the work measure would reach 100000 sprites at over a second a frame). About 1300 on the benchmark machine in 6 to 8 s, though one run in four stops lower on a noisy step. gate false until a baseline shows the noise of the number",
      "fast": false,
      "repetitions": 3,
      "timeoutSeconds": 120,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "sprites", "--ramp", "--measure", "cpu"],
      "metrics": {
        "objects_at_60fps": {"source": "json", "key": "objectsAt60fps", "unit": "objects", "better": "higher", "gate": false}
      }
    },
    {
      "name": "stress-entities-ramp",
      "about": "BagiStress entities ramp: the count at which work_ms fits 16.6 ms, about 150000 to 158000 nodes in 6 to 7 s over three runs (the line says verified true; a fixed run of 150000 nodes after 500 warm-up frames has work_ms_p50 16.7 ms). The tweens restart every frame at these counts. gate false until a baseline shows the noise of the number",
      "fast": false,
      "repetitions": 3,
      "timeoutSeconds": 180,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "entities", "--ramp"],
      "metrics": {
        "objects_at_60fps": {"source": "json", "key": "objectsAt60fps", "unit": "objects", "better": "higher", "gate": false}
      }
    },
    {
      "name": "stress-ui-ramp",
      "about": "BagiStress ui ramp: the count at which work_ms fits 16.6 ms, about 75000 labels in 9 s (the run to run spread is a few percent). gate false until a baseline shows the noise of the number",
      "fast": false,
      "repetitions": 3,
      "timeoutSeconds": 120,
      "command": ["{python}", "{engine}/tools/perf/stress_frames.py", "--exe", "{bin}/BagiStress", "--scene", "ui", "--ramp"],
      "metrics": {
        "objects_at_60fps": {"source": "json", "key": "objectsAt60fps", "unit": "objects", "better": "higher", "gate": false}
      }
    },
    {
      "name": "value-get",
      "about": "The cost of a Get of a Value<T> (ValueBenchTest, int32_t and Vec3): plain is a field read in place, const and slot are the two kinds of Value, tree_field and tree_slot end a chain of three virtual reads in a field or in a slot. Slot is expected within a few percent of plain; the trees show what the virtual calls add. The formula_* lines time one formula of about five operations over two objects: int32_t `caster.attack * (100 + cast.level * 10) / 100`, Vec3 `(caster.velocity + cast.target) * cast.power - caster.velocity * 0.5`; plain is C++, tree a heap object and a virtual Eval per step (the shape of a formula held as parts), program the compiled `expr` run with `Evaluate` in the interpreter, native the same program through its generated function (`Programs/Native/CLAUDE.md`; the test switches generated code off for program and unfused), unfused the same with `CompileOptions::fuse = false`, cached a `CachedValue` read with nothing changed. The madd_* lines time `slot * k + b` alone (int32_t `caster.attack * 3 + 7`, Vec3 `caster.velocity * 2 + vec3(1, 2, 3)`): program against unfused is what the peephole gains, native against program what generated code gains (Release: the formula about 6.5 times faster than the interpreter and 2.2 times plain C++). The mask_* lines time a formula over a game's registered type (the toy 256-bit mask: `popcount(or_mask(and_mask(a.mask, shift(b.mask, East)), c.mask)) > 3` over three objects, five CallDomain instructions): mask_plain is C++ over the mask, mask_program the interpreter, mask_run `Run` called directly, mask_native the generated function (Release: plain 1.2 ns, program 27, native 12). The trigger_* lines time a script of `Values/Effects/CLAUDE.md` over one cell (a timer counted down, a flag flipped and a command emitted at zero, or a mask narrowed and another command emitted): trigger_plain is C++ writing the same cell and pushing into the same `EmitBuffer`, trigger_script is `RunEffects` (Release: plain 4.4 ns, script 46, about 10 times; each includes the copy of the prototype cell and a digest). The ratios to watch: program against plain and against tree, native against program, unfused against program, cached against program. On the machine of the loop (lowest of three, with other worker loops running at load 40 to 77, so the ratios hold better than the absolute times): the program costs about 12 to 14 times plain C++ and about 1.8 to 2.1 times the virtual tree, (a program of one fused instruction already costs about 5 ns); fusing `slot * k + b` is worth 1.37 times for int32_t, and a cached read of two levels costs about as much as running a formula of this size (1.2 times the program for int32_t, 0.84 for Vec3). The iterations come from BE_VALUE_BENCH, set by env in the command (a run of about fifteen seconds in Release). Nothing here gates until the owner records a baseline. The list_* lines time a formula with a loop over the lists of a hand (count, any over a full loop and answered by the first element, sum, a loop in a loop) as a plain C++ loop, in the interpreter and as generated code.",
      "fast": true,
      "command": ["env", "BE_VALUE_BENCH=100000000", "{bin}/BagiEngineTests", "--gtest_filter=ValueBench*"],
      "metrics": {
        "int32_t_plain_ns": {"source": "regex", "pattern": "^ValueBench int32_t plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_const_ns": {"source": "regex", "pattern": "^ValueBench int32_t const ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_slot_ns": {"source": "regex", "pattern": "^ValueBench int32_t slot ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_tree_field_ns": {"source": "regex", "pattern": "^ValueBench int32_t tree_field ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_tree_slot_ns": {"source": "regex", "pattern": "^ValueBench int32_t tree_slot ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_formula_plain_ns": {"source": "regex", "pattern": "^ValueBench int32_t formula_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_formula_program_ns": {"source": "regex", "pattern": "^ValueBench int32_t formula_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_formula_native_ns": {"source": "regex", "pattern": "^ValueBench int32_t formula_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_formula_tree_ns": {"source": "regex", "pattern": "^ValueBench int32_t formula_tree ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "mask_plain_ns": {"source": "regex", "pattern": "^ValueBench mask_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "mask_program_ns": {"source": "regex", "pattern": "^ValueBench mask_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "mask_native_ns": {"source": "regex", "pattern": "^ValueBench mask_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_formula_unfused_ns": {"source": "regex", "pattern": "^ValueBench int32_t formula_unfused ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_formula_cached_ns": {"source": "regex", "pattern": "^ValueBench int32_t formula_cached ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_madd_plain_ns": {"source": "regex", "pattern": "^ValueBench int32_t madd_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_madd_program_ns": {"source": "regex", "pattern": "^ValueBench int32_t madd_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_madd_native_ns": {"source": "regex", "pattern": "^ValueBench int32_t madd_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_madd_unfused_ns": {"source": "regex", "pattern": "^ValueBench int32_t madd_unfused ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "trigger_plain_ns": {"source": "regex", "pattern": "^ValueBench trigger_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "trigger_script_ns": {"source": "regex", "pattern": "^ValueBench trigger_script ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_plain_ns": {"source": "regex", "pattern": "^ValueBench Vec3 plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_const_ns": {"source": "regex", "pattern": "^ValueBench Vec3 const ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_slot_ns": {"source": "regex", "pattern": "^ValueBench Vec3 slot ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_tree_field_ns": {"source": "regex", "pattern": "^ValueBench Vec3 tree_field ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_tree_slot_ns": {"source": "regex", "pattern": "^ValueBench Vec3 tree_slot ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_formula_plain_ns": {"source": "regex", "pattern": "^ValueBench Vec3 formula_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_formula_tree_ns": {"source": "regex", "pattern": "^ValueBench Vec3 formula_tree ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_formula_program_ns": {"source": "regex", "pattern": "^ValueBench Vec3 formula_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_formula_native_ns": {"source": "regex", "pattern": "^ValueBench Vec3 formula_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_formula_unfused_ns": {"source": "regex", "pattern": "^ValueBench Vec3 formula_unfused ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_formula_cached_ns": {"source": "regex", "pattern": "^ValueBench Vec3 formula_cached ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_madd_plain_ns": {"source": "regex", "pattern": "^ValueBench Vec3 madd_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_madd_program_ns": {"source": "regex", "pattern": "^ValueBench Vec3 madd_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_madd_native_ns": {"source": "regex", "pattern": "^ValueBench Vec3 madd_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "vec3_madd_unfused_ns": {"source": "regex", "pattern": "^ValueBench Vec3 madd_unfused ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_count_plain_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_count_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_count_program_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_count_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_count_native_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_count_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "bool_list_any_plain_ns": {"source": "regex", "pattern": "^ValueBench bool list_any_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "bool_list_any_program_ns": {"source": "regex", "pattern": "^ValueBench bool list_any_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "bool_list_any_native_ns": {"source": "regex", "pattern": "^ValueBench bool list_any_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "bool_list_any_early_plain_ns": {"source": "regex", "pattern": "^ValueBench bool list_any_early_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "bool_list_any_early_program_ns": {"source": "regex", "pattern": "^ValueBench bool list_any_early_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "bool_list_any_early_native_ns": {"source": "regex", "pattern": "^ValueBench bool list_any_early_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_sum_plain_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_sum_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_sum_program_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_sum_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_sum_native_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_sum_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_nested_plain_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_nested_plain ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_nested_program_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_nested_program ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false},
        "int32_t_list_nested_native_ns": {"source": "regex", "pattern": "^ValueBench int32_t list_nested_native ns_per_get=([0-9.]+)", "unit": "ns/get", "better": "lower", "gate": false}
      }
    },
    {
      "name": "websocket-handshake-scans",
      "about": "The slowest read of a request or response head of the largest allowed size, built of repeated tokens: the scan has to stay linear",
      "fast": false,
      "command": ["{bin}/BagiEngineTests", "--gtest_also_run_disabled_tests", "--gtest_filter=*HandshakeCostTest.DISABLED_*"],
      "metrics": {
        "server_scan_ms": {"source": "regex", "pattern": "^HandshakeCost server_scan_ms=([0-9.]+)", "unit": "ms", "better": "lower", "gate": false},
        "client_scan_ms": {"source": "regex", "pattern": "^HandshakeCost client_scan_ms=([0-9.]+)", "unit": "ms", "better": "lower", "gate": false}
      }
    },
    {
      "name": "block-pool-cost",
      "about": "An allocate and free of one block of a ConcurrentBlockPool against new and delete, on one and on four threads (ConcurrentBlockPoolCostTest): the pool has to stay cheaper than the heap and the per-thread shards keep four threads from slowing down",
      "fast": true,
      "command": ["{bin}/BagiEngineTests", "--gtest_also_run_disabled_tests", "--gtest_filter=ConcurrentBlockPoolCostTest.DISABLED_*"],
      "metrics": {
        "pooled_1t_ns": {"source": "regex", "pattern": "^ConcurrentBlockPoolCost threads=1 pooled_ns=([0-9.]+)", "unit": "ns/pair", "better": "lower", "gate": false},
        "heap_1t_ns": {"source": "regex", "pattern": "^ConcurrentBlockPoolCost threads=1 pooled_ns=[0-9.]+ heap_ns=([0-9.]+)", "unit": "ns/pair", "better": "lower", "gate": false},
        "pooled_4t_ns": {"source": "regex", "pattern": "^ConcurrentBlockPoolCost threads=4 pooled_ns=([0-9.]+)", "unit": "ns/pair", "better": "lower", "gate": false},
        "heap_4t_ns": {"source": "regex", "pattern": "^ConcurrentBlockPoolCost threads=4 pooled_ns=[0-9.]+ heap_ns=([0-9.]+)", "unit": "ns/pair", "better": "lower", "gate": false}
      }
    },
    {
      "name": "ui-web-limits",
      "about": "The web-style UI compiler on documents far over its limits (depth, nodes, a 10 MiB sheet, the cascade work, a descendant selector over a deep chain, a hostile cascade of long class attributes and selectors): the limits have to keep hostile files cheap",
      "fast": false,
      "command": ["{bin}/BagiEngineTests", "--gtest_also_run_disabled_tests", "--gtest_filter=UIWebTest.DISABLED_TheDocumentsAtTheLimitsCost"],
      "metrics": {
        "total_ms": {"source": "regex", "pattern": "^UIWebLimits total_ms=([0-9.]+)", "unit": "ms", "better": "lower", "gate": false}
      }
    },
    {
      "name": "engine-tests",
      "about": "BagiEngineTests in one process (it waits on threads and sockets, so wall time, not CPU)",
      "command": ["{bin}/BagiEngineTests", "--gtest_brief=1"],
      "metrics": {
        "wall_s": {"source": "wall", "unit": "s", "better": "lower"}
      }
    }
  ]
}
```

## Notes

- Headless, the present of the SDL software renderer is most of a frame whatever the scene draws; `work_ms` (the frame without the present) is the number that moves with scene and UI code.
- The `stress-*` benchmarks run `BagiStress` (`tools/stress/CLAUDE.md`) through `tools/perf/stress_frames.py`; the game measures itself (`work_ms` is the CPU time of a frame without the present, nearest-rank percentiles) and the ramp is a number per scene and machine (the scenes do not compare with one another). The stress scenes are the engine's frame benchmarks; the engine has no game of its own. Their baseline is the owner's word, like every other.
- `value-get` has a line per case and type (`ValueBench <type> <case> ns_per_get=<x>`: `plain`, `const`, `slot`, `tree_field`, `tree_slot`, then `formula_plain`, `formula_tree`, `formula_program`, `formula_native`, `formula_run` (printed, not in the set: `Run` without the look for a bound function), `formula_unfused`, `formula_cached` and `madd_plain`, `madd_program`, `madd_native`, `madd_unfused`; the `Vec3` `madd` program is fused like the numbers), then `ValueBench mask_<case>` with no type (`mask_plain`, `mask_program`, `mask_run` (printed, not in the set), `mask_native`), then `ValueBench trigger_plain` and `trigger_script` (a script of a cell over `RunEffects` against C++, no type), then `ValueBench int32_t list_<op>_<case>` and `ValueBench bool list_<op>_<case>` (`list_count`, `list_any`, `list_any_early`, `list_sum` and `list_nested`, each as `_plain`, `_program` and `_native`: a formula with a loop over a hand's lists against a C++ loop). Nothing in it gates. Only the ratios of one run mean anything, and a loaded machine (other worker loops) moves every one: take the lowest of several runs.
- The sentinel (`todo-merge`): `ask`.

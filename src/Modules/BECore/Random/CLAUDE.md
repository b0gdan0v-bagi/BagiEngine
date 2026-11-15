# Random (BECore)

`Rng` (`Random/Rng.h`, header only, no allocation, no `<random>`): a deterministic counter-based generator whose numbers are the same on every platform, compiler and optimization level. It is for what has to replay: a save that carries its generator, a recorded session, a bot arena, a scenario test. Anything that only needs "some" randomness (a debug colour, a particle jitter) does not need it. **One generator per game session**: a subsystem that wants its own stream takes the next number of the session's, so a replay has one thing to restore.

## State and save

`Rng` is `uint64_t seed` and `uint64_t counter`: 16 bytes, no padding, trivially copyable, standard layout (`static_assert`ed in the tests). A game saves it with `memcpy` (the bytes are the format; there is no reflected `Serialize`) and a restored copy continues with the same numbers. Draw `n` (1-based) does not depend on the earlier ones, so `At(n)` reads any draw without moving, and `Skip(n)` jumps; a replay may start in the middle.

## The algorithm is the contract

Draw `n` is `Mix(seed + n * 0x9E3779B97F4A7C15)` with Vigna's SplitMix64 finalizer, in `uint64_t` wrap-around arithmetic, so `Rng(seed)` followed by `Next64()` calls is the published SplitMix64 stream of that seed. The seed is used as it is: streams of two seeds overlap only when they differ by a multiple of the odd step constant, which no small integer seed does.

A saved replay relies on the exact result of each function below, so a change of any of them (the finalizer, the number of draws, the order of a shuffle, the rounding) breaks every replay in the wild: it needs a **new generator type**, never an edit of `Rng`.

| Function | Result |
|----------|--------|
| `Next64()` | one draw; the counter moves by 1 |
| `Next32()` | the high 32 bits of one `Next64()` |
| `Uniform01()` | `(Next64() >> 11) * 0x1p-53`, a `double` in [0, 1), exact on IEEE-754 |
| `UniformInt(lo, hi)` | both bounds inclusive, any integer type up to 64 bits, `lo <= hi` (`ASSERT`). `range = hi - lo + 1`; 0 (the full 64-bit range) returns one draw as it is. Else `limit = 2^64 mod range`, draw until `x >= limit`, result `lo + x % range`. No modulo bias; the number of draws depends only on the seed and the range. A range of one value draws once, so a stream does not depend on the width of a range |
| `Chance(p)` | `p <= 0` and NaN are false and `p >= 1` is true with **no** draw; otherwise one draw, `Uniform01() < p` |
| `WeightedPick(weights)` | `Optional<size_t>`; empty (nothing drawn) when there are no weights or all are 0. `total` is the `uint64_t` sum, `r = UniformInt<uint64_t>(0, total - 1)`, the first index whose running sum exceeds `r`; a zero weight is never picked |
| `Shuffle(Span<T>)` | Fisher-Yates from the end: for `i = n - 1` down to 1, `j = UniformInt<size_t>(0, i)`, swap `[i]` and `[j]`; 0 or 1 elements draw nothing. `T` needs `swap` only. Pass a `Span` (`Shuffle(Span<T>(items))`) |

Weights are integers on purpose: a float sum differs with compiler flags and the pick would differ with it. The caller scales its probabilities to integers.

## Reference numbers

Every expected number in `tests/BECore/RandomTest.cpp` comes from an independent implementation (a few lines of Python with every step masked to 64 bits), never from the C++ under test. A new case gets its numbers the same way: write the algorithm of the table above in Python, run it once outside the repository, paste the numbers.

## Benchmark

`tests/BECore/RandomBenchmarkTest.cpp` prints ns per call of `Next64`, `UniformInt(0, 5)` and `Shuffle` of 52 elements beside `std::mt19937_64`, `std::uniform_int_distribution` and `std::shuffle` (the one place `<random>` is used, marked `//-NoStd`). It asserts no time. Run it in a Release build, alone: `build/<preset>/bin/BagiEngineTests --gtest_filter='RandomBenchmark*'`; a Debug build runs a tenth of the work.

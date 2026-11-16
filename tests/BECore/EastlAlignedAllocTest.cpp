#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <new>

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace BECore::Tests {

    namespace {

        constexpr size_t kAlignment = 64;
        constexpr size_t kGrowthSteps = 100;

        struct alignas(kAlignment) Wide {
            char bytes[kAlignment];
        };

        /// Keeps the optimizer from removing an allocation whose result is unused
        void Use(void* ptr) {
            static std::atomic<void*> sink;
            sink.store(ptr, std::memory_order_relaxed);
        }

        bool IsAligned(const void* ptr) {
            return reinterpret_cast<uintptr_t>(ptr) % kAlignment == 0;
        }

#if defined(_WIN32) && defined(_DEBUG)

        constexpr int kCrtReportExitCode = 3;

        /// A mismatched free is not recoverable: the debug heap goes on to free the bad block after the report and the process
        /// hangs, so the hook names the report on stderr and ends the process, and a red run is quick and says why
        int ExitOnReportHook(int reportType, char* message, int* /*returnValue*/) {
            if (reportType == _CRT_WARN) {
                return 0;
            }
            std::fprintf(stderr, "EastlAlignedAllocTest: the Debug CRT reported (type %d): %s\n", reportType, message != nullptr ? message : "");
            std::fflush(stderr);
            std::_Exit(kCrtReportExitCode);
        }

        /// Ends the process on a report of the Debug CRT for its lifetime and checks the whole heap at every heap operation
        class CrtHeapWatch {
        public:
            CrtHeapWatch() {
                _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, ExitOnReportHook);
                _assertMode = _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
                _errorMode = _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
                _flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
                _CrtSetDbgFlag(_flags | _CRTDBG_CHECK_ALWAYS_DF);
            }

            ~CrtHeapWatch() {
                _CrtSetDbgFlag(_flags);
                _CrtSetReportMode(_CRT_ERROR, _errorMode);
                _CrtSetReportMode(_CRT_ASSERT, _assertMode);
                _CrtSetReportHook2(_CRT_RPTHOOK_REMOVE, ExitOnReportHook);
            }

            CrtHeapWatch(const CrtHeapWatch&) = delete;
            CrtHeapWatch& operator=(const CrtHeapWatch&) = delete;

            [[nodiscard]] bool HeapIsIntact() const {
                return _CrtCheckMemory() != 0;
            }

        private:
            int _assertMode = 0;
            int _errorMode = 0;
            int _flags = 0;
        };

#else

        /// Only the Windows Debug CRT checks its heap; elsewhere the test still runs the same allocations
        class CrtHeapWatch {
        public:
            [[nodiscard]] bool HeapIsIntact() const {
                return true;
            }
        };

#endif

    }  // namespace

    // EASTL allocates an over-aligned array with the aligned operator new[] and frees it with the plain operator delete[]. The tests
    // run that pair on every platform; under the Windows Debug CRT a block the CRT's free does not know ends the process (above).

    TEST(EastlAlignedAllocTest, AnOverAlignedVectorStaysAlignedThroughGrowthAndIsFreed) {
        CrtHeapWatch watch;
        {
            Vector<Wide> wide;
            for (size_t i = 0; i < kGrowthSteps; ++i) {
                wide.push_back(Wide{});
                EXPECT_TRUE(IsAligned(wide.data())) << "after " << i + 1 << " elements";
            }
        }
        EXPECT_TRUE(watch.HeapIsIntact());
    }

    TEST(EastlAlignedAllocTest, APlainDeleteFreesAnAlignedNew) {
        CrtHeapWatch watch;
        constexpr size_t kSize = 3 * kAlignment;

        void* single = ::operator new(kSize, std::align_val_t{kAlignment});
        Use(single);
        EXPECT_TRUE(IsAligned(single));
        ::operator delete(single);

        void* array = ::operator new[](kSize, std::align_val_t{kAlignment});
        Use(array);
        EXPECT_TRUE(IsAligned(array));
        ::operator delete[](array);

        void* sized = ::operator new[](kSize, std::align_val_t{kAlignment});
        Use(sized);
        ::operator delete[](sized, kSize);

        EXPECT_TRUE(watch.HeapIsIntact());
    }

}  // namespace BECore::Tests

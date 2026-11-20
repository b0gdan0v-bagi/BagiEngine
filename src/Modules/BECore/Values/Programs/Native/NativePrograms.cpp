#include "NativePrograms.h"

namespace BECore {

    namespace {

        constexpr size_t kExpectedEntriesPerHash = 1;

        class Registry {
        public:
            static Registry& Instance() {
                static Registry registry;
                return registry;
            }

            void Add(Span<const NativeProgramEntry> entries) {
                LockGuard lock(_mutex);
                for (const NativeProgramEntry& entry : entries) {
                    if (entry.fn != nullptr) {
                        _byHash[entry.codeHash].push_back(&entry);
                    }
                }
            }

            bool Empty() const {
                LockGuard lock(_mutex);
                return _byHash.empty();
            }

            NativeProgramFn Find(const Program& program) const {
                if (!_enabled.load(std::memory_order_acquire)) {
                    return nullptr;
                }
                const uint64_t hash = program.CodeHash();
                LockGuard lock(_mutex);
                const auto bucket = _byHash.find(hash);
                if (bucket == _byHash.end()) {
                    return nullptr;
                }
                const ProgramCodeView view = program.CodeView();
                for (const NativeProgramEntry* entry : bucket->second) {
                    if (entry->CodeView() == view) {
                        return entry->fn;
                    }
                }
                return nullptr;
            }

            std::atomic<bool>& Enabled() {
                return _enabled;
            }

            std::atomic<bool>& WarnOnMiss() {
                return _warnOnMiss;
            }

        private:
            Registry() = default;

            mutable Mutex _mutex{BE_LOCK_SITE("NativePrograms")};
            HashMap<uint64_t, SmallVector<const NativeProgramEntry*, kExpectedEntriesPerHash>> _byHash;
            std::atomic<bool> _enabled{true};
            std::atomic<bool> _warnOnMiss{false};
        };

    }  // namespace

    void NativePrograms::Install(Span<const NativeProgramEntry> entries) {
        Registry::Instance().Add(entries);
        ProgramPool::RebindNative();
    }

    void NativePrograms::SetEnabled(bool enabled) {
        Registry::Instance().Enabled().store(enabled, std::memory_order_release);
        ProgramPool::RebindNative();
    }

    bool NativePrograms::IsEnabled() {
        return Registry::Instance().Enabled().load(std::memory_order_acquire);
    }

    void NativePrograms::SetWarnOnMiss(bool warn) {
        Registry::Instance().WarnOnMiss().store(warn, std::memory_order_release);
    }

    NativeProgramCounts NativePrograms::Counts() {
        NativeProgramCounts counts;
        counts.bound = ProgramPool::NativeCount();
        counts.missed = IsEnabled() ? ProgramPool::Count() - counts.bound : 0;
        return counts;
    }

    bool NativePrograms::HasGeneratedCode() {
        return !Registry::Instance().Empty();
    }

    void NativePrograms::LogSummary() {
        const NativeProgramCounts counts = Counts();
        LOG_INFO(Format("Native programs: {} bound, {} missed", counts.bound, counts.missed));
    }

    NativeProgramFn NativePrograms::Find(const Program& program) {
        return Registry::Instance().Find(program);
    }

    void NativePrograms::ReportMiss(PoolString source) {
        if (!IsEnabled() || !Registry::Instance().WarnOnMiss().load(std::memory_order_acquire)) {
            return;
        }
        const StringView text = source.ToStringView();
        const String message =
            Format("Native programs: no generated function for the formula '{}': the generated programs file is stale or missing, regenerate it with the tool's --emit-native-programs; "
                   "this formula runs in the interpreter",
                   text.empty() ? StringView("(assembled by hand)") : text);
        LOG_WARNING(message);
    }

}  // namespace BECore

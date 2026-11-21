#include "NativePrograms.h"

#include <BECore/Values/Programs/Domain/ProgramDomain.h>

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
                    if (entry.fn == nullptr) {
                        continue;
                    }
                    if (entry.domainFingerprint != 0) {
                        _pending.push_back(Pending{&entry, false});
                    } else {
                        _byHash[entry.codeHash].push_back(&entry);
                    }
                }
            }

            /// Moves the waiting entries whose fingerprint equals the process's now into the table; returns how many still wait and were not reported yet when @p report is on
            size_t BindPending(bool report) {
                LockGuard lock(_mutex);
                if (_pending.empty()) {
                    return 0;
                }
                const uint64_t fingerprint = ProgramDomain::Fingerprint();
                size_t kept = 0;
                size_t unreported = 0;
                for (Pending& waiting : _pending) {
                    if (waiting.entry->domainFingerprint == fingerprint) {
                        _byHash[waiting.entry->codeHash].push_back(waiting.entry);
                        continue;
                    }
                    if (report && !waiting.reported) {
                        waiting.reported = true;
                        ++unreported;
                    }
                    _pending[kept++] = waiting;
                }
                _pending.resize(kept);
                return unreported;
            }

            bool Empty() const {
                LockGuard lock(_mutex);
                return _byHash.empty() && _pending.empty();
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
            struct Pending {
                const NativeProgramEntry* entry = nullptr;
                bool reported = false;
            };

            Registry() = default;

            mutable Mutex _mutex{BE_LOCK_SITE("NativePrograms")};
            HashMap<uint64_t, SmallVector<const NativeProgramEntry*, kExpectedEntriesPerHash>> _byHash;
            Vector<Pending> _pending;
            std::atomic<bool> _enabled{true};
            std::atomic<bool> _warnOnMiss{false};
        };

    }  // namespace

    void NativePrograms::Install(Span<const NativeProgramEntry> entries) {
        Registry::Instance().Add(entries);
        BindPending();
    }

    void NativePrograms::BindPending() {
        const bool warn = Registry::Instance().WarnOnMiss().load(std::memory_order_acquire);
        const size_t unreported = Registry::Instance().BindPending(warn);
        ProgramPool::RebindNative();
        if (unreported > 0) {
            LOG_WARNING(Format("Native programs: {} generated functions were made for another set of registered types and operations (ProgramDomain::Fingerprint) and are not used; "
                               "register the game's types and operations in the order the tool does, before ApplyNativeProgramsFlags(); those programs run in the interpreter",
                               unreported));
        }
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

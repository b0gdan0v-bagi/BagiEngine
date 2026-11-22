#include "ChromeTraceBackend.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceJson.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerClock.h>
#include <bit>
#include <cstdlib>
#include <new>
#include <thread>

namespace BECore {

    namespace {

        CORE_ENUM(TraceEventType, uint8_t, Begin, End, Counter, Frame)

        /// The recorders of the process get ids 1, 2, ...; a thread caches the buffer of the last one it wrote to
        constinit std::atomic<uint64_t> g_nextRecorderId{1};
        constinit thread_local uint64_t t_cachedRecorder = 0;
        constinit thread_local void* t_cachedBuffer = nullptr;

        constexpr double kNanosecondsPerMicrosecond = 1000.0;
        constexpr size_t kMaxAllocNames = 100;

    }  // namespace

    struct ChromeTraceBackend::Event {
        uint64_t ticks;
        const void* data;  // the ProfileSite of a zone, the name of a counter
        uint64_t a;        // End: allocation count; Counter: the value's bits
        uint64_t b;        // End: allocated bytes
        TraceEventType type;
    };

    struct ChromeTraceBackend::ThreadBuffer {
        std::thread::id owner;
        uint32_t threadId = 0;
        std::atomic<uint32_t> count{0};       // events [0, count) are complete; written by the owner only
        std::atomic<uint32_t> startIndex{0};  // the first event of the trace (MarkTraceStart)
        Event* chunks[kMaxChunks]{};          // chunk k is set by the owner before count passes k * kChunkEvents
    };

    ChromeTraceBackend::ChromeTraceBackend() : _id(g_nextRecorderId.fetch_add(1, std::memory_order_relaxed)), _startTicks(ProfilerClock::Now()) {}

    ChromeTraceBackend::~ChromeTraceBackend() {
        const uint32_t count = _bufferCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < count; ++i) {
            ThreadBuffer* buffer = _buffers[i];
            for (Event* chunk : buffer->chunks) {
                std::free(chunk);
            }
            buffer->~ThreadBuffer();
            std::free(buffer);
        }
    }

    ChromeTraceBackend::ThreadBuffer* ChromeTraceBackend::GetThreadBuffer() {
        if (t_cachedRecorder == _id) {
            return static_cast<ThreadBuffer*>(t_cachedBuffer);
        }

        const std::thread::id self = std::this_thread::get_id();
        std::lock_guard lock(_registryMutex);  //-NoStd: profiler lock (Threading/CLAUDE.md)
        ThreadBuffer* found = nullptr;
        const uint32_t count = _bufferCount.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < count && found == nullptr; ++i) {
            if (_buffers[i]->owner == self) {
                found = _buffers[i];
            }
        }
        if (found == nullptr) {
            if (count == kMaxThreadBuffers) {
                return nullptr;
            }
            // std::malloc, not new: recording must not enter the global operator new that ENGINE_PROFILER_ALLOC counts
            void* memory = std::malloc(sizeof(ThreadBuffer));
            if (memory == nullptr) {
                return nullptr;
            }
            found = new (memory) ThreadBuffer();
            found->owner = self;
            found->threadId = Profiler::CurrentThreadId();
            _buffers[count] = found;
            _bufferCount.store(count + 1, std::memory_order_release);
        }
        t_cachedRecorder = _id;
        t_cachedBuffer = found;
        return found;
    }

    void ChromeTraceBackend::Record(uint8_t type, const void* data, uint64_t a, uint64_t b, uint64_t ticks) {
        ThreadBuffer* buffer = GetThreadBuffer();
        if (buffer == nullptr) {
            _dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const uint32_t index = buffer->count.load(std::memory_order_relaxed);
        const uint32_t chunkIndex = index / kChunkEvents;
        if (chunkIndex >= kMaxChunks) {
            _dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        Event*& chunk = buffer->chunks[chunkIndex];
        if (chunk == nullptr) {
            chunk = static_cast<Event*>(std::malloc(sizeof(Event) * kChunkEvents));
            if (chunk == nullptr) {
                _dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        chunk[index % kChunkEvents] = Event{ticks, data, a, b, static_cast<TraceEventType>(type)};
        buffer->count.store(index + 1, std::memory_order_release);
    }

    uint64_t ChromeTraceBackend::BeginZone(const ProfileSite& site) {
        Record(+TraceEventType::Begin, &site, 0, 0, ProfilerClock::Now());
        return 0;
    }

    void ChromeTraceBackend::EndZone(uint64_t /*token*/, const ProfileSite& site, const ZoneAllocs& allocs) {
        Record(+TraceEventType::End, &site, allocs.count, allocs.bytes, ProfilerClock::Now());
    }

    void ChromeTraceBackend::RecordZone(const ProfileSite& site, uint64_t beginTicks, uint64_t endTicks) {
        // The thread was blocked between the two ticks, so nothing of its own lies between them and the pair nests. A full buffer drops
        // the pair as a whole: a lone E would close an outer zone early
        ThreadBuffer* buffer = GetThreadBuffer();
        if (buffer == nullptr || buffer->count.load(std::memory_order_relaxed) + 2 > kMaxEventsPerThread) {
            _dropped.fetch_add(2, std::memory_order_relaxed);
            return;
        }
        Record(+TraceEventType::Begin, &site, 0, 0, beginTicks);
        Record(+TraceEventType::End, &site, 0, 0, endTicks);
    }

    void ChromeTraceBackend::FrameMark() {
        Record(+TraceEventType::Frame, nullptr, 0, 0, ProfilerClock::Now());
    }

    void ChromeTraceBackend::Counter(const char* name, double value) {
        Record(+TraceEventType::Counter, name, std::bit_cast<uint64_t>(value), 0, ProfilerClock::Now());
    }

    void ChromeTraceBackend::SetThreadName(const char* /*name*/) {
        // The names live in Profiler (they outlive a recorder and reach the ones made later); WriteJson reads them from there
    }

    void ChromeTraceBackend::MarkTraceStart() {
        // By index, not by time stamp: the clock ticks coarser than events follow each other. A buffer registered later holds only later events
        std::lock_guard lock(_registryMutex);  //-NoStd: profiler lock (Threading/CLAUDE.md)
        _startTicks.store(ProfilerClock::Now(), std::memory_order_relaxed);
        const uint32_t count = _bufferCount.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < count; ++i) {
            _buffers[i]->startIndex.store(_buffers[i]->count.load(std::memory_order_acquire), std::memory_order_relaxed);
        }
    }

    uint64_t ChromeTraceBackend::GetDroppedCount() const {
        return _dropped.load(std::memory_order_relaxed);
    }

    String ChromeTraceBackend::WriteJson() const {
        constexpr int kPid = 1;
        const uint64_t start = _startTicks.load(std::memory_order_relaxed);
        const auto timestamp = [start](uint64_t ticks) { return ticks > start ? ProfilerClock::ToNanoseconds(ticks - start) / kNanosecondsPerMicrosecond : 0.0; };

        std::lock_guard lock(_registryMutex);  //-NoStd: profiler lock (Threading/CLAUDE.md); against MarkTraceStart and a buffer being registered, a recording thread takes it only to register its
                                               // buffer, on its first event
        String out;
        out.append("{\"traceEvents\":[");
        bool first = true;
        const auto beginEvent = [&out, &first] {
            out.append(first ? "\n" : ",\n");
            first = false;
        };

        Vector<const ProfileSite*> open;
        const uint32_t bufferCount = _bufferCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < bufferCount; ++i) {
            const ThreadBuffer& buffer = *_buffers[i];
            const uint32_t tid = buffer.threadId;

            if (const char* name = Profiler::GetThreadName(tid)) {
                beginEvent();
                AppendThreadNameEvent(out, kPid, tid, name);
                if (const uint32_t sortIndex = Profiler::GetThreadSortIndex(tid); sortIndex != Profiler::kNoSortIndex) {
                    beginEvent();
                    AppendThreadSortIndexEvent(out, kPid, tid, sortIndex);
                }
            }

            open.clear();
            double lastTs = 0.0;
            const uint32_t count = buffer.count.load(std::memory_order_acquire);
            for (uint32_t index = buffer.startIndex.load(std::memory_order_relaxed); index < count; ++index) {
                const Event& event = buffer.chunks[index / kChunkEvents][index % kChunkEvents];
                const double ts = timestamp(event.ticks);
                lastTs = ts;
                switch (event.type) {
                    case TraceEventType::Begin: {
                        const auto* site = static_cast<const ProfileSite*>(event.data);
                        open.push_back(site);
                        beginEvent();
                        AppendZoneBeginEvent(out, kPid, tid, ts, *site);
                        break;
                    }
                    case TraceEventType::End:
                        if (open.empty()) {
                            break;  // its Begin was before the start of the trace
                        }
                        open.pop_back();
                        beginEvent();
                        AppendZoneEndEvent(out, kPid, tid, ts, event.a, event.b);
                        break;
                    case TraceEventType::Counter:
                        beginEvent();
                        out.append("{\"name\":");
                        AppendJsonString(out, static_cast<const char*>(event.data));
                        out.append(Format(",\"ph\":\"C\",\"pid\":{},\"tid\":{},\"ts\":{:.3f},\"args\":{{\"value\":{}}}}}", kPid, tid, ts, std::bit_cast<double>(event.a)));
                        break;
                    case TraceEventType::Frame:
                        beginEvent();
                        out.append(Format("{{\"name\":\"Frame\",\"ph\":\"i\",\"s\":\"g\",\"pid\":{},\"tid\":{},\"ts\":{:.3f}}}", kPid, tid, ts));
                        break;
                }
            }
            // Zones still running (a dump in the middle of the run) end at the thread's last event, so every B has its E
            for (size_t k = 0; k < open.size(); ++k) {
                beginEvent();
                out.append(Format("{{\"ph\":\"E\",\"pid\":{},\"tid\":{},\"ts\":{:.3f}}}", kPid, tid, lastTs));
            }
        }

        out.append(Format("\n],\"displayTimeUnit\":\"ms\",\"otherData\":{{\"dropped\":{}", GetDroppedCount()));
        if constexpr (BE_PROFILER_ALLOC != 0) {
            out.append(",\"allocNames\":[");
            size_t written = 0;
            for (const AllocSiteStats& stats : AllocTracker::GetNameStats()) {
                if (written == kMaxAllocNames) {
                    break;
                }
                out.append(written++ == 0 ? "\n{\"zone\":" : ",\n{\"zone\":");
                AppendJsonString(out, stats.ZoneLabel());
                out.append(",\"name\":");
                AppendJsonString(out, stats.name != nullptr ? stats.name : "");
                out.append(Format(",\"allocs\":{},\"bytes\":{}}}", stats.count, stats.bytes));
            }
            out.append("]");
        }
        out.append("}}\n");
        return out;
    }

    std::expected<void, String> ChromeTraceBackend::WriteFile(StringView path) const {
        return WriteTextFile(path, WriteJson());
    }

}  // namespace BECore

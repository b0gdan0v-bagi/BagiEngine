#pragma once



#include <cstddef>

namespace BECore {

    struct LogEntry {
        static constexpr size_t MaxMessageSize = 512;

        LogLevel level = LogLevel::Debug;
        uint16_t messageLength = 0;
        char message[MaxMessageSize] = {};
    };

    /**
     * @brief Lock-free MPSC ring buffer for log entries.
     *
     * Based on Dmitry Vyukov's bounded MPMC queue adapted for single-consumer use.
     * Multiple threads may call TryPush() concurrently without locks.
     * Only ONE thread may call Drain() at a time (main thread / log flush thread).
     *
     * @tparam Capacity Fixed number of slots. Must be a power of 2.
     */
    template <size_t Capacity = 4096>
    class LogRingBuffer {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
        static constexpr size_t Mask = Capacity - 1;

        struct Cell {
            std::atomic<size_t> sequence{0};
            LogEntry entry;
        };

    public:
        LogRingBuffer() noexcept {
            for (size_t i = 0; i < Capacity; ++i) {
                _buffer[i].sequence.store(i, std::memory_order_relaxed);
            }
        }

        /**
         * @brief Push a log entry into the buffer (lock-free, thread-safe).
         *
         * Copies up to LogEntry::MaxMessageSize-1 characters from message.
         * Messages that exceed the limit are truncated.
         *
         * @return true on success, false if the buffer is full (entry dropped).
         */
        bool TryPush(LogLevel level, eastl::string_view message) noexcept {
            size_t pos = _writeHead.load(std::memory_order_relaxed);

            for (;;) {
                Cell& cell = _buffer[pos & Mask];
                size_t seq = cell.sequence.load(std::memory_order_acquire);
                auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos);

                if (diff == 0) {
                    // Slot is free — try to claim it atomically
                    if (_writeHead.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                        // Write payload
                        LogEntry& entry = cell.entry;
                        entry.level = level;
                        const size_t len = std::min(message.size(), LogEntry::MaxMessageSize - 1);
                        entry.messageLength = static_cast<uint16_t>(len);
                        std::memcpy(entry.message, message.data(), len);
                        entry.message[len] = '\0';

                        // Signal consumer that the entry is ready
                        cell.sequence.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                    // Another producer claimed this slot — reload and retry
                } else if (diff < 0) {
                    // Wrapped around: buffer is full
                    _dropped.fetch_add(1, std::memory_order_relaxed);
                    return false;
                } else {
                    // Another producer is ahead — load fresh head and retry
                    pos = _writeHead.load(std::memory_order_relaxed);
                }
            }
        }

        /**
         * @brief Drain all ready entries, invoking callback for each one.
         *
         * Must be called from a single consumer thread only.
         *
         * @tparam Callback  void(const LogEntry&)
         * @return Number of entries processed.
         */
        template <typename Callback>
        size_t Drain(Callback&& callback) noexcept(noexcept(callback(std::declval<const LogEntry&>()))) {
            size_t count = 0;

            for (;;) {
                Cell& cell = _buffer[_readHead & Mask];
                size_t seq = cell.sequence.load(std::memory_order_acquire);
                auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(_readHead + 1);

                if (diff != 0) {
                    break; // Entry not yet ready (or buffer empty)
                }

                callback(cell.entry);

                // Release the slot back to producers
                cell.sequence.store(_readHead + Capacity, std::memory_order_release);
                ++_readHead;
                ++count;
            }

            return count;
        }

        /**
         * @brief Number of entries dropped due to a full buffer since startup.
         */
        size_t GetDroppedCount() const noexcept {
            return _dropped.load(std::memory_order_relaxed);
        }

        /**
         * @brief Process-wide singleton ring buffer instance.
         */
        static LogRingBuffer& GetGlobal() noexcept {
            static LogRingBuffer instance;
            return instance;
        }

    private:
        // Separate cache lines to prevent false sharing between producers and consumer
        alignas(64) std::atomic<size_t> _writeHead{0};
        alignas(64) size_t _readHead{0};
        alignas(64) std::atomic<size_t> _dropped{0};

        Cell _buffer[Capacity];
    };

} // namespace BECore

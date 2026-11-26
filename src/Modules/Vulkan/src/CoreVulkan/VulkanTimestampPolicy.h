#pragma once

#include <cstdint>

/**
 * The arithmetic of Vulkan timestamp queries as pure functions. Header only and free of any Vulkan call, so a test checks them without a
 * device (`tests/BECore/VulkanTimestampPolicyTest.cpp`).
 */
namespace BECore::VulkanTimestampPolicy {

    inline constexpr uint32_t kTimestampBits = 64;
    inline constexpr double kNanosecondsPerMillisecond = 1.0e6;

    /// The bits of a query result the queue family writes (`VkQueueFamilyProperties::timestampValidBits`); the rest is undefined
    [[nodiscard]] constexpr uint64_t ValidBitsMask(uint32_t validBits) {
        // A shift by the whole width is undefined
        return validBits >= kTimestampBits ? UINT64_MAX : (uint64_t{1} << validBits) - 1;
    }

    /// Whether a queue can time: it writes timestamps and the device says how long a tick is
    [[nodiscard]] constexpr bool CanTime(uint32_t validBits, float periodNs) {
        return validBits > 0 && periodNs > 0.0f;
    }

    /// The ticks from @p begin forward to @p end. Taken modulo the valid bits, so a counter that wrapped once in between still gives the span
    [[nodiscard]] constexpr uint64_t TicksBetween(uint64_t begin, uint64_t end, uint32_t validBits) {
        return (end - begin) & ValidBitsMask(validBits);
    }

    /// The milliseconds from the timestamp @p begin to @p end, @p periodNs nanoseconds per tick; 0 when the queue cannot time
    [[nodiscard]] constexpr double ElapsedMs(uint64_t begin, uint64_t end, uint32_t validBits, float periodNs) {
        if (!CanTime(validBits, periodNs)) {
            return 0.0;
        }
        return static_cast<double>(TicksBetween(begin, end, validBits)) * static_cast<double>(periodNs) / kNanosecondsPerMillisecond;
    }

    /// Whether @p later is ahead of @p earlier on the counter. A counter that wraps has no order of its own: ahead by more than half of its
    /// range reads as behind
    [[nodiscard]] constexpr bool IsAfter(uint64_t later, uint64_t earlier, uint32_t validBits) {
        const uint64_t ahead = TicksBetween(earlier, later, validBits);
        return ahead != 0 && ahead <= ValidBitsMask(validBits) / 2;
    }

    /// The two timestamps of one frame
    struct TimestampSpan {
        uint64_t begin = 0;
        uint64_t end = 0;
    };

    /**
     * @brief Whether @p span can measure a frame newer than the one of @p previous (null before the first): it does not end before it begins,
     * and each of its timestamps is after the same one of @p previous.
     *
     * A driver may answer with success and the timestamps of an earlier frame, or with one of them (KosmicKrisp does in frames that follow
     * each other closely). Those are not the time of any frame.
     */
    [[nodiscard]] constexpr bool IsNewSpan(const TimestampSpan* previous, TimestampSpan span, uint32_t validBits) {
        if (IsAfter(span.begin, span.end, validBits)) {
            return false;
        }
        return previous == nullptr || (IsAfter(span.begin, previous->begin, validBits) && IsAfter(span.end, previous->end, validBits));
    }

}  // namespace BECore::VulkanTimestampPolicy

#pragma once

#include <BECore/Types/Types.h>

// Compiled only on macOS and never built in the loop that wrote it (Hardware/CLAUDE.md, "Compiled where")
#ifdef PLATFORM_MACOS

#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace BECore {

    /**
     * @brief Owns one Core Foundation object and releases it with CFRelease.
     *
     * The Create/Copy rule: every function with `Create` or `Copy` in its name returns an object the caller owns (+1), and the result
     * goes into a CfOwned at once. What `Get` functions and CFDictionaryGetValue return is borrowed from its container: it is never
     * released and never kept after the container dies. @p T is the pointer type of the object (CFDictionaryRef, CFTypeRef, ...).
     */
    template <typename T>
    class CfOwned {
    public:
        CfOwned() = default;
        explicit CfOwned(T object) : _object(object) {}
        ~CfOwned() {
            Reset();
        }

        CfOwned(const CfOwned&) = delete;
        CfOwned& operator=(const CfOwned&) = delete;
        CfOwned(CfOwned&& other) noexcept : _object(other.Release()) {}
        CfOwned& operator=(CfOwned&& other) noexcept {
            if (this != &other) {
                Reset(other.Release());
            }
            return *this;
        }

        [[nodiscard]] T Get() const {
            return _object;
        }
        explicit operator bool() const {
            return _object != nullptr;
        }

        /// Gives the object up without releasing it
        [[nodiscard]] T Release() {
            T object = _object;
            _object = nullptr;
            return object;
        }

        /// Releases the held object and takes @p object (which this then owns)
        void Reset(T object = nullptr) {
            if (_object != nullptr) {
                CFRelease(_object);
            }
            _object = object;
        }

    private:
        T _object = nullptr;
    };

    /// The value of @p key in @p dictionary, borrowed; nullptr when the key is absent
    [[nodiscard]] inline CFTypeRef CfDictionaryValue(CFDictionaryRef dictionary, const char* key) {
        const CfOwned<CFStringRef> cfKey(CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8));
        if (!cfKey) {
            return nullptr;
        }
        return CFDictionaryGetValue(dictionary, cfKey.Get());
    }

    /// The number under @p key as a double; nothing when the key is absent, is not a CFNumber or does not convert exactly
    [[nodiscard]] inline Optional<double> CfNumberAsDouble(CFDictionaryRef dictionary, const char* key) {
        const CFTypeRef value = CfDictionaryValue(dictionary, key);
        if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID()) {
            return std::nullopt;
        }
        double result = 0.0;
        if (CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &result) == 0) {
            return std::nullopt;
        }
        return result;
    }

    /// The number under @p key as an unsigned integer; nothing when the key is absent, is not a CFNumber, is negative or is not whole
    [[nodiscard]] inline Optional<uint64_t> CfNumberAsUInt64(CFDictionaryRef dictionary, const char* key) {
        const CFTypeRef value = CfDictionaryValue(dictionary, key);
        if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID()) {
            return std::nullopt;
        }
        int64_t result = 0;
        if (CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &result) == 0 || result < 0) {
            return std::nullopt;
        }
        return static_cast<uint64_t>(result);
    }

    /**
     * @brief Copies a CFString, or a CFData that holds a NUL-terminated text (how the IORegistry gives `model`), into @p out as UTF-8.
     *
     * Always leaves @p out terminated; false (and an empty text) when the value is null, of another type, empty, or a CFString that does not fit; a CFData is cut to fit.
     */
    inline bool CfStringToBuffer(CFTypeRef value, Span<char> out) {
        if (out.empty()) {
            return false;
        }
        out[0] = '\0';
        if (value == nullptr) {
            return false;
        }
        const CFTypeID type = CFGetTypeID(value);
        if (type == CFStringGetTypeID()) {
            if (CFStringGetCString(static_cast<CFStringRef>(value), out.data(), static_cast<CFIndex>(out.size()), kCFStringEncodingUTF8) == 0) {
                out[0] = '\0';
                return false;
            }
        } else if (type == CFDataGetTypeID()) {
            const CFDataRef data = static_cast<CFDataRef>(value);
            const uint8_t* const bytes = CFDataGetBytePtr(data);
            if (bytes == nullptr) {
                return false;
            }
            const size_t length = std::min(static_cast<size_t>(std::max<CFIndex>(CFDataGetLength(data), 0)), out.size() - 1);
            size_t count = 0;
            while (count < length && bytes[count] != 0) {
                ++count;
            }
            std::memcpy(out.data(), bytes, count);
            out[count] = '\0';
        } else {
            return false;
        }
        return out[0] != '\0';
    }

}  // namespace BECore

#endif  // PLATFORM_MACOS

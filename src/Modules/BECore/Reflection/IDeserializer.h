#pragma once

#include <BECore/Reflection/ISerializer.h>

namespace BECore {

    /**
     * @brief Error information for deserialization failures
     */
    struct DeserializeError {
        PoolString path;          // Full path to the field, e.g. "player.inventory.items[3].name"
        PoolString errorMessage;
        int32_t line = 0;         // Line number for XML (0 for binary)
    };

    /**
     * @brief Interface for deserialization (reading data)
     *
     * Provides methods for reading primitive types and strings from various
     * storage formats (XML, binary, etc.). Includes error tracking for
     * reporting missing or malformed data.
     *
     * Path tracking is maintained automatically when subclasses call
     * PushPathSegment / PushPathArrayIndex / PopPathSegment in their
     * BeginObject / EndObject / BeginArray / EndArray / BeginArrayElement /
     * EndArrayElement overrides.
     *
     * @example
     * XmlDeserializer deserializer;
     * deserializer.LoadFromFile("save.xml");
     * deserializer.BeginObject("player");
     * int32_t health;
     * deserializer.ReadAttribute("health", health);
     * deserializer.EndObject();
     * 
     * if (deserializer.HasErrors()) {
     *     for (const auto& err : deserializer.GetErrors()) {
     *         LOG_ERROR("Deserialization error at '{}': {}", err.path, err.errorMessage);
     *     }
     * }
     */
    class IDeserializer : public IArchiveBase {
    public:
        virtual ~IDeserializer() = default;

        // =================================================================
        // Primitive types deserialization (from child elements)
        // =================================================================

        virtual bool Read(eastl::string_view name, bool& outValue) = 0;
        virtual bool Read(eastl::string_view name, int8_t& outValue) = 0;
        virtual bool Read(eastl::string_view name, uint8_t& outValue) = 0;
        virtual bool Read(eastl::string_view name, int16_t& outValue) = 0;
        virtual bool Read(eastl::string_view name, uint16_t& outValue) = 0;
        virtual bool Read(eastl::string_view name, int32_t& outValue) = 0;
        virtual bool Read(eastl::string_view name, uint32_t& outValue) = 0;
        virtual bool Read(eastl::string_view name, int64_t& outValue) = 0;
        virtual bool Read(eastl::string_view name, uint64_t& outValue) = 0;
        virtual bool Read(eastl::string_view name, float& outValue) = 0;
        virtual bool Read(eastl::string_view name, double& outValue) = 0;

        // =================================================================
        // String types deserialization (from child elements)
        // =================================================================

        virtual bool Read(eastl::string_view name, eastl::string& outValue) = 0;
        virtual bool Read(eastl::string_view name, PoolString& outValue) = 0;

        // =================================================================
        // Attribute deserialization (from XML attributes, inline for binary)
        // =================================================================

        virtual bool ReadAttribute(eastl::string_view name, bool& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, int8_t& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, uint8_t& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, int16_t& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, uint16_t& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, int32_t& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, uint32_t& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, int64_t& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, uint64_t& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, float& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, double& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, eastl::string& outValue) = 0;
        virtual bool ReadAttribute(eastl::string_view name, PoolString& outValue) = 0;

        // =================================================================
        // Error tracking
        // =================================================================

        /**
         * @brief Check if any errors occurred during deserialization
         * @return true if there are any errors
         */
        virtual bool HasErrors() const = 0;

        /**
         * @brief Get all deserialization errors
         * @return Vector of errors encountered
         */
        virtual const eastl::vector<DeserializeError>& GetErrors() const = 0;

        /**
         * @brief Clear all accumulated errors
         */
        virtual void ClearErrors() = 0;

        // =================================================================
        // Path-based error reporting (non-virtual, uses path stack)
        // =================================================================

        /**
         * @brief Build the full path string from the current path stack + key
         *
         * Format: "player.inventory.items[3].name"
         */
        eastl::string GetCurrentPath(eastl::string_view leafKey = {}) const {
            eastl::string result;
            result.reserve(64);
            for (const auto& seg : _pathStack) {
                if (seg.isArrayIndex) {
                    result += '[';
                    result += seg.name;
                    result += ']';
                } else {
                    if (!result.empty())
                        result += '.';
                    result += seg.name;
                }
            }
            if (!leafKey.empty()) {
                if (!result.empty())
                    result += '.';
                result.append(leafKey.data(), leafKey.size());
            }
            return result;
        }

        /**
         * @brief Report an error at the current path + key
         *
         * Builds the full path and delegates to AddError().
         *
         * @param key  Leaf key where the error occurred
         * @param message  Human-readable error description
         */
        void ReportError(eastl::string_view key, eastl::string_view message) {
            eastl::string fullPath = GetCurrentPath(key);
            AddError(eastl::string_view(fullPath.data(), fullPath.size()), message, 0);
        }

    protected:
        IDeserializer() = default;
        IDeserializer(const IDeserializer&) = default;
        IDeserializer& operator=(const IDeserializer&) = default;
        IDeserializer(IDeserializer&&) = default;
        IDeserializer& operator=(IDeserializer&&) = default;

        /**
         * @brief Subclasses call this to register a named path segment
         *        (BeginObject, BeginArray)
         */
        void PushPathSegment(eastl::string_view name) {
            PathSegment seg;
            seg.name.assign(name.data(), name.size());
            seg.isArrayIndex = false;
            _pathStack.push_back(eastl::move(seg));
        }

        /**
         * @brief Subclasses call this to register an array index segment
         *        (BeginArrayElement)
         */
        void PushPathArrayIndex(size_t index) {
            PathSegment seg;
            // Convert index to decimal string without external dependencies
            char buf[24];
            char* p = buf + sizeof(buf);
            *--p = '\0';
            size_t n = index;
            do {
                *--p = static_cast<char>('0' + (n % 10));
                n /= 10;
            } while (n != 0);
            seg.name.assign(p);
            seg.isArrayIndex = true;
            _pathStack.push_back(eastl::move(seg));
        }

        /**
         * @brief Subclasses call this in EndObject / EndArray / EndArrayElement
         */
        void PopPathSegment() {
            if (!_pathStack.empty())
                _pathStack.pop_back();
        }

        /**
         * @brief Store an error -- override in concrete deserializers
         *
         * Default implementation does nothing; XmlDeserializer overrides.
         */
        virtual void AddError(eastl::string_view path, eastl::string_view message, int32_t line) = 0;

    private:
        struct PathSegment {
            eastl::string name;
            bool isArrayIndex = false;
        };
        eastl::vector<PathSegment> _pathStack;
    };

}  // namespace BECore

#pragma once

#include <BECore/Reflection/ISerializer.h>
#include <EASTL/vector.h>

namespace BECore {

    /**
     * @brief Error information for deserialization failures
     */
    struct DeserializeError {
        PoolString fieldName;
        PoolString errorMessage;
        int32_t line = 0;  // Line number for XML (0 for binary)
    };

    /**
     * @brief Interface for deserialization (reading data)
     *
     * Provides methods for reading primitive types and strings from various
     * storage formats (XML, binary, etc.). Includes error tracking for
     * reporting missing or malformed data.
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
     *         LOG_ERROR("Error: {}", err.errorMessage);
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

    protected:
        IDeserializer() = default;
        IDeserializer(const IDeserializer&) = default;
        IDeserializer& operator=(const IDeserializer&) = default;
        IDeserializer(IDeserializer&&) = default;
        IDeserializer& operator=(IDeserializer&&) = default;
    };

}  // namespace BECore

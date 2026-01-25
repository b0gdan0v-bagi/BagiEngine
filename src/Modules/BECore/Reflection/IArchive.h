#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <BECore/PoolString/PoolString.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief Abstract interface for serialization archives
     *
     * Provides a unified interface for reading and writing data to various
     * storage formats (XML, binary, JSON, etc.). The same archive interface
     * is used for both serialization and deserialization.
     *
     * @example
     * // Writing
     * XmlArchive archive(XmlArchive::Mode::Write);
     * archive.BeginObject("player");
     * archive.Serialize("health", player.health);
     * archive.Serialize("name", player.name);
     * archive.EndObject();
     * archive.SaveToFile("save.xml");
     *
     * // Reading
     * XmlArchive archive(XmlArchive::Mode::Read);
     * archive.LoadFromFile("save.xml");
     * archive.BeginObject("player");
     * archive.Serialize("health", player.health);
     * archive.Serialize("name", player.name);
     * archive.EndObject();
     */
    class IArchive {
    public:
        virtual ~IArchive() = default;

        // =================================================================
        // Mode checking
        // =================================================================

        /**
         * @brief Check if archive is in reading mode
         * @return true if deserializing data
         */
        virtual bool IsReading() const = 0;

        /**
         * @brief Check if archive is in writing mode
         * @return true if serializing data
         */
        virtual bool IsWriting() const = 0;

        // =================================================================
        // Primitive types serialization (as child elements)
        // =================================================================

        virtual void Serialize(eastl::string_view name, bool& value) = 0;
        virtual void Serialize(eastl::string_view name, int8_t& value) = 0;
        virtual void Serialize(eastl::string_view name, uint8_t& value) = 0;
        virtual void Serialize(eastl::string_view name, int16_t& value) = 0;
        virtual void Serialize(eastl::string_view name, uint16_t& value) = 0;
        virtual void Serialize(eastl::string_view name, int32_t& value) = 0;
        virtual void Serialize(eastl::string_view name, uint32_t& value) = 0;
        virtual void Serialize(eastl::string_view name, int64_t& value) = 0;
        virtual void Serialize(eastl::string_view name, uint64_t& value) = 0;
        virtual void Serialize(eastl::string_view name, float& value) = 0;
        virtual void Serialize(eastl::string_view name, double& value) = 0;

        // =================================================================
        // String types serialization (as child elements)
        // =================================================================

        virtual void Serialize(eastl::string_view name, eastl::string& value) = 0;
        virtual void Serialize(eastl::string_view name, PoolString& value) = 0;

        // =================================================================
        // Attribute serialization (for XML attributes, inline for binary)
        // =================================================================
        // Use SerializeAttribute for primitive values that should be stored
        // as XML attributes rather than child elements. This is the preferred
        // method for configuration files where compactness matters.

        virtual void SerializeAttribute(eastl::string_view name, bool& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, int8_t& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, uint8_t& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, int16_t& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, uint16_t& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, int32_t& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, uint32_t& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, int64_t& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, uint64_t& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, float& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, double& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, eastl::string& value) = 0;
        virtual void SerializeAttribute(eastl::string_view name, PoolString& value) = 0;

        // =================================================================
        // Object nesting
        // =================================================================

        /**
         * @brief Begin a nested object/section
         *
         * When writing: creates a new named section
         * When reading: navigates into the named section
         *
         * @param name Name of the nested object
         * @return true if object exists (for reading) or was created (for writing)
         */
        virtual bool BeginObject(eastl::string_view name) = 0;

        /**
         * @brief End the current nested object/section
         */
        virtual void EndObject() = 0;

        // =================================================================
        // Array support
        // =================================================================

        /**
         * @brief Begin an array section
         *
         * When writing: creates a new named array, count is the number of elements
         * When reading: navigates into the array, count receives the element count
         *
         * @param name Name of the array
         * @param count [in/out] Number of elements
         * @return true if array exists or was created
         */
        virtual bool BeginArray(eastl::string_view name, size_t& count) = 0;

        /**
         * @brief End the current array section
         */
        virtual void EndArray() = 0;

    protected:
        IArchive() = default;
        IArchive(const IArchive&) = default;
        IArchive& operator=(const IArchive&) = default;
        IArchive(IArchive&&) = default;
        IArchive& operator=(IArchive&&) = default;
    };

}  // namespace BECore

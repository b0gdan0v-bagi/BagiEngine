#pragma once

#include <BECore/Reflection/IArchive.h>
#include <EASTL/vector.h>
#include <filesystem>
#include <cstddef>

namespace BECore {

    /**
     * @brief Binary archive implementation for compact serialization
     *
     * Provides efficient binary serialization for save files where file size
     * and performance are more important than human readability.
     *
     * Format notes:
     * - Strings are stored as: uint32_t length + char data (no null terminator)
     * - Arrays are stored as: uint32_t count + elements
     * - Objects have no overhead (fields are stored sequentially)
     *
     * @example
     * // Writing
     * BinaryArchive archive(BinaryArchive::Mode::Write);
     * Serialize(archive, "player", player);
     * archive.SaveToFile("save.bin");
     *
     * // Reading
     * BinaryArchive archive(BinaryArchive::Mode::Read);
     * archive.LoadFromFile("save.bin");
     * Serialize(archive, "player", player);
     */
    class BinaryArchive : public IArchive {
    public:
        enum class Mode {
            Read,
            Write
        };

        /**
         * @brief Construct a binary archive
         * @param mode Read or Write mode
         */
        explicit BinaryArchive(Mode mode);
        ~BinaryArchive() override = default;

        // Non-copyable, movable
        BinaryArchive(const BinaryArchive&) = delete;
        BinaryArchive& operator=(const BinaryArchive&) = delete;
        BinaryArchive(BinaryArchive&&) = default;
        BinaryArchive& operator=(BinaryArchive&&) = default;

        // =================================================================
        // File I/O
        // =================================================================

        /**
         * @brief Load binary data from file
         * @param path File path to load from
         * @return true on success
         */
        bool LoadFromFile(const std::filesystem::path& path);

        /**
         * @brief Load binary data from buffer
         * @param data Pointer to data
         * @param size Size of data in bytes
         * @return true on success
         */
        bool LoadFromBuffer(const void* data, size_t size);

        /**
         * @brief Save binary data to file
         * @param path File path to save to
         * @return true on success
         */
        bool SaveToFile(const std::filesystem::path& path) const;

        /**
         * @brief Get binary data buffer
         * @return Reference to internal buffer
         */
        const eastl::vector<uint8_t>& GetBuffer() const { return _buffer; }

        /**
         * @brief Get current buffer size
         */
        size_t GetSize() const { return _buffer.size(); }

        // =================================================================
        // IArchive interface
        // =================================================================

        bool IsReading() const override { return _mode == Mode::Read; }
        bool IsWriting() const override { return _mode == Mode::Write; }

        void Serialize(eastl::string_view name, bool& value) override;
        void Serialize(eastl::string_view name, int8_t& value) override;
        void Serialize(eastl::string_view name, uint8_t& value) override;
        void Serialize(eastl::string_view name, int16_t& value) override;
        void Serialize(eastl::string_view name, uint16_t& value) override;
        void Serialize(eastl::string_view name, int32_t& value) override;
        void Serialize(eastl::string_view name, uint32_t& value) override;
        void Serialize(eastl::string_view name, int64_t& value) override;
        void Serialize(eastl::string_view name, uint64_t& value) override;
        void Serialize(eastl::string_view name, float& value) override;
        void Serialize(eastl::string_view name, double& value) override;
        void Serialize(eastl::string_view name, eastl::string& value) override;
        void Serialize(eastl::string_view name, PoolString& value) override;

        bool BeginObject(eastl::string_view name) override;
        void EndObject() override;

        bool BeginArray(eastl::string_view name, size_t& count) override;
        void EndArray() override;

    private:
        /**
         * @brief Write raw bytes to buffer
         */
        void WriteBytes(const void* data, size_t size);

        /**
         * @brief Read raw bytes from buffer
         * @return true if enough data available
         */
        bool ReadBytes(void* data, size_t size);

        /**
         * @brief Write a POD value
         */
        template <typename T>
        void WritePOD(const T& value) {
            WriteBytes(&value, sizeof(T));
        }

        /**
         * @brief Read a POD value
         */
        template <typename T>
        bool ReadPOD(T& value) {
            return ReadBytes(&value, sizeof(T));
        }

        Mode _mode;
        eastl::vector<uint8_t> _buffer;
        size_t _readPos = 0;
    };

}  // namespace BECore

#pragma once

#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Types/Types.h>
#include <Common/TestFiles.h>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The bytes of a cooked content file: the stamp of @p rootTypeName, then @p object as its root scope
    template <typename T>
    Vector<uint8_t> StampedContentBytes(StringView rootTypeName, const T& object) {
        BinarySerializer writer;
        EXPECT_TRUE(WriteContentStamp(writer, rootTypeName)) << "no reflected type " << String(rootTypeName).c_str();
        object.Serialize(writer);
        return writer.GetData();
    }

    /// As StampedContentBytes, with a layout hash of the caller's choice: a file that a program of another layout cooked
    template <typename T>
    Vector<uint8_t> StampedContentBytesWithLayout(StringView rootTypeName, uint64_t layout, const T& object) {
        BinarySerializer writer;
        EXPECT_TRUE(writer.BeginObject(BinaryFormat::ContentStampName));
        writer.Write("root", rootTypeName);
        writer.Write("layout", layout);
        writer.EndObject();
        object.Serialize(writer);
        return writer.GetData();
    }

    /// The bytes of a document that has no stamp, as a save writes it
    template <typename T>
    Vector<uint8_t> UnstampedContentBytes(const T& object) {
        BinarySerializer writer;
        object.Serialize(writer);
        return writer.GetData();
    }

    /// Writes @p bytes to @p file, making the folders above it
    inline void WriteContentBytes(const std::filesystem::path& file, const Vector<uint8_t>& bytes) {
        WriteFile(file, StringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    /// Writes the cooked file of @p object to @p file (`<folder>/config/<Name>.bin`): stamped for @p rootTypeName
    template <typename T>
    void WriteContentBin(const std::filesystem::path& file, StringView rootTypeName, const T& object) {
        WriteContentBytes(file, StampedContentBytes(rootTypeName, object));
    }

}  // namespace BECore::Tests

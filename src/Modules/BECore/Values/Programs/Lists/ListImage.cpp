#include "ListImage.h"

namespace BECore {

    namespace {

        constexpr size_t kStrideAt = sizeof(uint32_t);
        constexpr size_t kCountAt = 2 * sizeof(uint32_t);
        constexpr size_t kLayoutAt = 3 * sizeof(uint32_t);

        constexpr size_t kMaxField = std::numeric_limits<uint32_t>::max();

    }  // namespace

    RegisterImage ListDescriptor::ToImage() const {
        RegisterImage image{};
        uint8_t* bytes = image.bytes.data();
        std::memcpy(bytes, &offset, sizeof(offset));
        std::memcpy(bytes + kStrideAt, &stride, sizeof(stride));
        std::memcpy(bytes + kCountAt, &count, sizeof(count));
        bytes[kLayoutAt] = static_cast<uint8_t>(layout);
        return image;
    }

    ListDescriptor ListDescriptor::FromImage(const RegisterImage& image) {
        ListDescriptor descriptor;
        const uint8_t* bytes = image.bytes.data();
        std::memcpy(&descriptor.offset, bytes, sizeof(descriptor.offset));
        std::memcpy(&descriptor.stride, bytes + kStrideAt, sizeof(descriptor.stride));
        std::memcpy(&descriptor.count, bytes + kCountAt, sizeof(descriptor.count));
        descriptor.layout = static_cast<ListLayout>(bytes[kLayoutAt]);
        return descriptor;
    }

    std::expected<ListDescriptor, String> MakeListDescriptor(size_t offset, const TypeDesc& list) {
        if (list.kind != TypeKind::List) {
            return std::unexpected(Format("a loop needs a list field, but the field is a {}", TypeKindName(list.kind)));
        }
        if (list.elementSize == 0) {
            return std::unexpected(String("the elements of the list have no size"));
        }
        if (offset > kMaxField) {
            return std::unexpected(Format("the list is at offset {}, which does not fit 32 bits", offset));
        }
        if (list.elementSize > kMaxField) {
            return std::unexpected(Format("an element of the list is {} bytes, which does not fit 32 bits", list.elementSize));
        }
        ListDescriptor descriptor;
        descriptor.offset = static_cast<uint32_t>(offset);
        descriptor.stride = static_cast<uint32_t>(list.elementSize);
        if (list.resizable) {
            descriptor.layout = ListLayout::Pointers;
            return descriptor;
        }
        if (list.capacity > kMaxField) {
            return std::unexpected(Format("the array has {} elements, which does not fit 32 bits", list.capacity));
        }
        descriptor.layout = ListLayout::Inline;
        descriptor.count = static_cast<uint32_t>(list.capacity);
        return descriptor;
    }

}  // namespace BECore

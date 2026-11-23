#pragma once

#include <BECore/Types/Types.h>
#include <pugixml.hpp>

namespace BECore {

    /// Collects what pugixml saves into a String: `document.save(writer, indent, flags)` or `node.print(writer, ...)`, then read `text`
    struct StringXmlWriter final : pugi::xml_writer {
        String text;

        StringXmlWriter() = default;
        /// pugixml hands the text over in blocks of a couple of KiB: a caller that knows the size reserves it instead of growing by doubling
        explicit StringXmlWriter(size_t reserveBytes) {
            text.reserve(reserveBytes);
        }

        void write(const void* data, size_t size) override {
            text.append(static_cast<const char*>(data), size);
        }
    };

}  // namespace BECore

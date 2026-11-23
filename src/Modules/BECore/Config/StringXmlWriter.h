#pragma once

#include <BECore/Types/Types.h>
#include <pugixml.hpp>

namespace BECore {

    /// Collects what pugixml saves into a String: `document.save(writer, indent, flags)` or `node.print(writer, ...)`, then read `text`
    struct StringXmlWriter final : pugi::xml_writer {
        String text;

        void write(const void* data, size_t size) override {
            text.append(static_cast<const char*>(data), size);
        }
    };

}  // namespace BECore

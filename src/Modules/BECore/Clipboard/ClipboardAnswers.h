#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// `clipboard_set`
    struct ClipboardSetAnswer {
        BE_CLASS(ClipboardSetAnswer)

        BE_REFLECT_FIELD bool ok = false;
    };

    /// `clipboard_get`: `status` is the lower case word of `ToString(ClipboardStatus)`, which a reflected enum would not write; `text` is a view of the
    /// text the waiting call holds, which can be megabytes and is not copied
    struct ClipboardGetAnswer {
        BE_CLASS(ClipboardGetAnswer)

        BE_REFLECT_FIELD PoolString status;
        BE_REFLECT_FIELD TextView text;
    };

}  // namespace BECore

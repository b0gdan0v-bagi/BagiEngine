#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// A document of `editor_state`; the texts are views into the states the handler holds while it writes the answer, `kind` is lower camel case
    struct EditorStateDocument {
        BE_CLASS(EditorStateDocument)

        BE_REFLECT_FIELD bool dirty = false;
        BE_REFLECT_FIELD TextView file;
        BE_REFLECT_FIELD TextView id;
        BE_REFLECT_FIELD PoolString kind;
        BE_REFLECT_FIELD TextView name;
        BE_REFLECT_FIELD TextView path;
    };

    /// `editor_state`
    struct EditorStateAnswer {
        BE_CLASS(EditorStateAnswer)

        BE_REFLECT_FIELD bool anyDirty = false;
        BE_REFLECT_FIELD ArenaVector<EditorStateDocument> documents;
    };

}  // namespace BECore

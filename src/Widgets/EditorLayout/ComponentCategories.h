#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Types/Types.h>

namespace BECore {

    struct ClassMeta;
    class SceneNode;

    /// The colour of `meta.category`: that of a built-in category, else the one the active game declared (GameEditorTools::AddCategory). Empty, which draws as
    /// neutral, for a class that names none and for an unknown name, which is logged once per name. Safe to call every frame: nothing is allocated for a name
    /// that was seen. Names must outlive the call (they are literals of the generated `ClassMeta`).
    Optional<Color> CategoryColor(const ClassMeta& meta);

    /// The meta of the node's first component, in the node's order, whose class names a category; null when none does
    const ClassMeta* CategoryOfNode(const SceneNode& node);

    /// CategoryColor of CategoryOfNode; empty for a node without a category
    Optional<Color> CategoryColorOfNode(const SceneNode& node);

}  // namespace BECore

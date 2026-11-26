#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class ReflectedConfigDocument;

    /**
     * @brief Draws the objects of a `ReflectedConfigDocument` with ImGui and edits them in place
     *
     * The root's own fields come first, then the factory lists, element by element: a header, the `type` combo, the `enabled` box
     * (not for a type that reflects `enabled`) and the fields. The part of the Config Editor that stays C++ next to
     * `ConfigTreeDrawer`: the fields are drawn by `ImGuiPropertyVisitor` from the reflected types. A new type or a toggled box is
     * applied after the walk, because it replaces what the walk reads. The labels are made by `Prepare`, so a frame without an
     * edit allocates nothing of its own.
     */
    class ReflectedConfigDrawer {
    public:
        /// Makes the labels of @p document; once for each document that is opened, before its first `Draw`
        void Prepare(const ReflectedConfigDocument& document);

        /// Draws @p document, the one prepared last, and tells it about the edits
        void Draw(ReflectedConfigDocument& document);

    private:
        struct ListLabels {
            String title;
            Vector<String> typeNames;  // in the order of the list's types
            Vector<String> headers;    // one per element
        };

        Vector<ListLabels> _lists;
        String _refusal;  // why the last choice of a type changed nothing
    };

}  // namespace BECore

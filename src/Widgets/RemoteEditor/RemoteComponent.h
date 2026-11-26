#pragma once

#include <BECore/Control/JsonTape.h>
#include <BECore/Types/Types.h>
#include <Widgets/RemoteEditor/RemoteFieldWalker.h>

namespace BECore {

    /**
     * @brief A component of a node of the attached program, read from the result of its `scene_get` method (the answer with all fields)
     *
     * Holds the node path, the type name, which namesake of the node it is (the answer does not say: the one who asked knows) and the fields.
     */
    class RemoteComponent {
    public:
        RemoteComponent() = default;
        RemoteComponent(const RemoteComponent&) = delete;
        RemoteComponent& operator=(const RemoteComponent&) = delete;
        RemoteComponent(RemoteComponent&&) = default;
        RemoteComponent& operator=(RemoteComponent&&) = default;

        /// Rebuilds from @p result when its text or @p index differs from the last one; true when it did
        bool Load(StringView result, size_t index);

        void Clear();

        /// A component was read: the last result had the shape of the answer
        [[nodiscard]] bool IsLoaded() const {
            return !_type.empty();
        }

        [[nodiscard]] StringView GetNode() const {
            return _node;
        }

        [[nodiscard]] StringView GetType() const {
            return _type;
        }

        [[nodiscard]] size_t GetIndex() const {
            return _index;
        }

        /**
         * @brief Visits the fields as `AcceptPropertyVisitor` of the local component does (`RemoteFieldWalker`)
         *
         * Nothing is visited when @p schema does not know the type. A change goes into @p writes, which also say what an edited field shows.
         *
         * @return true when an edit went into @p writes
         */
        bool Walk(const RemoteSchema& schema, RemotePendingWrites& writes, IPropertyVisitor& visitor);

    private:
        /// A vector, so that a move keeps the bytes the tape borrows where they are
        Vector<char> _text;
        JsonTape _tape;
        /// The token of the object of the fields
        uint32_t _fields = 0;
        String _node;
        String _type;
        size_t _index = 0;
        RemoteFieldWalker _walker;
    };

}  // namespace BECore

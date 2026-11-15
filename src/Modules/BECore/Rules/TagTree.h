#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace BECore {

    /// The number of a tag after TagTree::Bake().
    using TagId = uint16_t;

    /// The most tags a tree holds; the ids are 0 .. kMaxTagCount - 1.
    inline constexpr size_t kMaxTagCount = 65535;

    /// An inclusive range of tag ids. A tag and its whole subtree are one range; `lo > hi` is the empty range.
    struct TagRange {
        TagId lo = 1;
        TagId hi = 0;

        [[nodiscard]] constexpr bool IsEmpty() const {
            return lo > hi;
        }

        [[nodiscard]] constexpr bool Contains(TagId tag) const {
            return lo <= tag && tag <= hi;
        }

        constexpr bool operator==(const TagRange&) const = default;
    };

    /**
     * @brief Hierarchical tags written as dotted paths (`match.3`), numbered so that a subtree is a range of ids.
     *
     * `Add` declares tags (a path creates every ancestor), `Bake` numbers them by a depth-first preorder walk with the children in the
     * order of their first `Add`: a tag and all its descendants are the contiguous ids `[id, id + subtree size - 1]`, so "is this tag
     * under that one" is two comparisons. The questions are valid after `Bake`, a later `Add` is a contract violation. A segment
     * of a path is not empty and has no `*` and no whitespace (a selector text could not name it). Copyable: a `RuleAxis` keeps its
     * own copy.
     */
    class TagTree {
    public:
        /// Declares the tag and its ancestors; declaring a known tag changes nothing. On an error the tree is unchanged.
        [[nodiscard]] std::expected<void, String> Add(StringView path);

        /// Numbers the tags; a second call changes nothing.
        void Bake();

        [[nodiscard]] bool IsBaked() const {
            return _baked;
        }

        /// The number of tags (valid before Bake too).
        [[nodiscard]] size_t Count() const {
            return _nodes.size() - 1;
        }

        /// The id of the tag with exactly this full path.
        [[nodiscard]] Optional<TagId> Find(StringView path) const;

        /// The tag and its descendants.
        [[nodiscard]] TagRange Range(TagId tag) const;

        /// The descendants without the tag: empty for a leaf.
        [[nodiscard]] TagRange DescendantRange(TagId tag) const;

        /// The number of segments of the path: a root tag is 1.
        [[nodiscard]] uint32_t Depth(TagId tag) const;

        /// The full path of the tag.
        [[nodiscard]] StringView Name(TagId tag) const;

    private:
        struct Node {
            String path;
            uint32_t depth = 0;
            TagId id = 0;
            TagId last = 0;
            Vector<uint32_t> children;
        };

        [[nodiscard]] const Node& NodeOf(TagId tag) const;

        /// Node 0 is the root above the tags and is not a tag; the others are in the order of creation.
        Vector<Node> _nodes = Vector<Node>(1);
        HashMap<String, uint32_t> _index;
        /// The node of every id after Bake.
        Vector<uint32_t> _byId;
        bool _baked = false;
    };

}  // namespace BECore

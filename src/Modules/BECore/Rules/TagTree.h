#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace BECore {

    /// The number of a tag after TagTree::Bake().
    using TagId = uint16_t;

    /// The id that is never a tag: the parent of a root.
    inline constexpr TagId kNoTag = 0xFFFF;

    /// The most segments of a tag path (`a.b.c` is 3).
    inline constexpr size_t kMaxTagDepth = 8;

    /// The most characters of a full dotted tag path.
    inline constexpr size_t kMaxTagNameLength = 64;

    /// The most tags a tree holds, implied ancestors included; the ids are 0 .. kMaxTagCount - 1.
    inline constexpr size_t kMaxTagCount = 512;

    /// The most bytes the full names of all tags take, one NUL after each (the name blob of the baked form).
    inline constexpr size_t kMaxTagNameBlob = 8192;

    static_assert(kMaxTagCount < kNoTag, "every tag id must differ from kNoTag");

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
     * under that one" is two comparisons. The questions are valid after `Bake`, a later `Add` is a contract violation. A path has
     * 1..kMaxTagDepth non-empty segments of `[A-Za-z0-9_]` and at most kMaxTagNameLength characters. Copyable: a `RuleAxis` keeps its
     * own copy.
     */
    class TagTree {
    public:
        /// Declares the tag and its ancestors; declaring a known tag changes nothing. An error names the tag (and the limit) and leaves
        /// the tree unchanged.
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

        /// The id of the parent, kNoTag for a root.
        [[nodiscard]] TagId Parent(TagId tag) const;

        /// The tag and its descendants.
        [[nodiscard]] TagRange Range(TagId tag) const;

        /// The descendants without the tag: empty for a leaf.
        [[nodiscard]] TagRange DescendantRange(TagId tag) const;

        /// The number of segments of the path: a root tag is 1.
        [[nodiscard]] uint32_t Depth(TagId tag) const;

        /// The full path of the tag.
        [[nodiscard]] StringView Name(TagId tag) const;

        /// The id of the tag with exactly this path; no wildcard. The error names the path.
        [[nodiscard]] std::expected<TagId, String> ResolveTag(StringView path) const;

        /**
         * @brief The ids a pattern stands for: `x` is `{id, id}`; `x.*` is `Range(id)` (the subtree **with** `x`, unlike
         *        DescendantRange); `*` is every tag (empty on an empty tree).
         *
         * `*` stands alone or is the whole last segment after a dot; any other `*` is an error, and so is an unknown name (also the
         * prefix of `x.*`). The error names the pattern.
         */
        [[nodiscard]] std::expected<TagRange, String> Resolve(StringView pattern) const;

    private:
        struct Node {
            String path;
            /// The index in _nodes; 0 (the root above the tags) for a root tag.
            uint32_t parent = 0;
            uint32_t depth = 0;
            TagId id = 0;
            TagId last = 0;
            Vector<uint32_t> children;
        };

        [[nodiscard]] const Node& NodeOf(TagId tag) const;

        /// Node 0 is the root above the tags and is not a tag; the others are in the order of creation.
        Vector<Node> _nodes = Vector<Node>(1);
        HashMap<String, uint32_t> _index;
        /// Bytes of the full names of all tags, one NUL after each.
        size_t _nameBytes = 0;
        /// The node of every id after Bake.
        Vector<uint32_t> _byId;
        bool _baked = false;
    };

}  // namespace BECore

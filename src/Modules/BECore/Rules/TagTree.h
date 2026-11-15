#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>

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
     * @brief The baked form of a TagTree: plain data a game keeps in its content blob and copies with memcpy.
     *
     * No pointers and no containers (fixed arrays sized by the kMaxTag* constants), trivially copyable and standard layout. Fill it with
     * TagTree::FillBaked on a block that is zero-initialised (`BakedTagTree block{}`); every byte past the data stays zero, so the same
     * tree gives byte-equal blocks. The ids and ranges are those of the tree it came from. The lookups allocate nothing on success; an
     * id that is not a tag (`>= Count()`, `kNoTag`) gives `kNoTag`, an empty name or an empty range, never an assert.
     */
    struct BakedTagTree {
        /// The number of tags.
        uint16_t count;
        /// Bytes of `names` in use, the NUL after the last name included.
        uint16_t nameBlobSize;
        /// The id of the parent, kNoTag for a root.
        TagId parent[kMaxTagCount];
        /// The last id of the subtree of the tag (its own id for a leaf).
        TagId subtreeEnd[kMaxTagCount];
        /// The offset in `names` of the full path of the tag.
        uint16_t nameOffset[kMaxTagCount];
        /// The full dotted paths in id order, each followed by a NUL.
        char names[kMaxTagNameBlob];

        [[nodiscard]] size_t Count() const {
            return count;
        }

        /// The id of the tag with exactly this full path. A linear scan over the names: for load time, not for a frame.
        [[nodiscard]] Optional<TagId> Find(StringView path) const;

        [[nodiscard]] TagId Parent(TagId tag) const;

        /// The tag and its descendants; empty for an id that is not a tag.
        [[nodiscard]] TagRange Range(TagId tag) const;

        /// The descendants without the tag; empty for a leaf and for an id that is not a tag.
        [[nodiscard]] TagRange DescendantRange(TagId tag) const;

        /// The number of segments of the path: a root tag is 1, an id that is not a tag 0 (counted along the parent chain).
        [[nodiscard]] uint32_t Depth(TagId tag) const;

        /// The full path of the tag, a view into the blob; empty for an id that is not a tag.
        [[nodiscard]] StringView Name(TagId tag) const;

        /// As TagTree::ResolveTag.
        [[nodiscard]] std::expected<TagId, String> ResolveTag(StringView path) const;

        /// As TagTree::Resolve.
        [[nodiscard]] std::expected<TagRange, String> Resolve(StringView pattern) const;
    };

    static_assert(std::is_trivially_copyable_v<BakedTagTree>, "BakedTagTree is memcpy'd");         //-NoStd: a type trait, no EASTL form
    static_assert(std::is_standard_layout_v<BakedTagTree>, "BakedTagTree is a plain data block");  //-NoStd: a type trait, no EASTL form
    static_assert(sizeof(BakedTagTree) == 4 + 3 * sizeof(TagId) * kMaxTagCount + kMaxTagNameBlob, "no padding: the unused bytes are the zero fill");

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

        /// Writes the baked form of the tree into `out` (the tree must be baked): the whole block is zeroed first, so the same tree gives
        /// byte-equal blocks.
        void FillBaked(BakedTagTree& out) const;

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

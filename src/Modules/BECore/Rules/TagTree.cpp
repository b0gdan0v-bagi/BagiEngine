#include "TagTree.h"

namespace BECore {

    namespace {

        constexpr char kSeparator = '.';
        constexpr char kWildcard = '*';
        constexpr StringView kAnyText = "*";
        constexpr StringView kSubtreeSuffix = ".*";

        bool IsSegmentCharacter(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }

        bool EndsWith(StringView text, StringView suffix) {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        /// What is wrong with a path as a phrase that follows the path's name, empty when the path is a valid tag.
        String CheckPath(StringView path) {
            if (path.empty()) {
                return "the path is empty";
            }
            if (path.size() > kMaxTagNameLength) {
                return Format("the path is longer than {} characters", kMaxTagNameLength);
            }
            size_t depth = 0;
            size_t segmentStart = 0;
            while (true) {
                const size_t dot = path.find(kSeparator, segmentStart);
                const size_t segmentEnd = dot == StringView::npos ? path.size() : dot;
                if (segmentEnd == segmentStart) {
                    return "the path has an empty segment";
                }
                for (size_t i = segmentStart; i < segmentEnd; ++i) {
                    if (IsSegmentCharacter(path[i])) {
                        continue;
                    }
                    if (path[i] == kWildcard) {
                        return "the path has a '*' (a wildcard is only `*` or the whole last segment `x.*` of a pattern)";
                    }
                    return Format("the path has the character '{}' (a segment is [A-Za-z0-9_])", path[i]);
                }
                ++depth;
                if (dot == StringView::npos) {
                    break;
                }
                segmentStart = dot + 1;
            }
            if (depth > kMaxTagDepth) {
                return Format("the path is deeper than {} segments", kMaxTagDepth);
            }
            return {};
        }

        enum class PatternKind : uint8_t { Tag, Subtree, All };

        /// A pattern text split into the tag path to look up and how much of the tree it takes.
        struct ParsedPattern {
            PatternKind kind = PatternKind::Tag;
            StringView path;
        };

        ParsedPattern ParsePattern(StringView pattern) {
            if (pattern == kAnyText) {
                return {PatternKind::All, {}};
            }
            if (EndsWith(pattern, kSubtreeSuffix)) {
                return {PatternKind::Subtree, pattern.substr(0, pattern.size() - kSubtreeSuffix.size())};
            }
            return {PatternKind::Tag, pattern};
        }

        /// The one copy of the lookup rules, for TagTree and BakedTagTree: `Tree` has `Count()`, `Find(path)` giving `Optional<TagId>`
        /// and `Range(id)`.
        template <class Tree>
        std::expected<TagId, String> ResolveTagIn(const Tree& tree, StringView path) {
            if (String problem = CheckPath(path); !problem.empty()) {
                return std::unexpected(Format("tag '{}': {}", path, problem));
            }
            const Optional<TagId> id = tree.Find(path);
            if (!id) {
                return std::unexpected(Format("unknown tag '{}'", path));
            }
            return *id;
        }

        template <class Tree>
        std::expected<TagRange, String> ResolveIn(const Tree& tree, StringView pattern) {
            const ParsedPattern parsed = ParsePattern(pattern);
            if (parsed.kind == PatternKind::All) {
                return tree.Count() == 0 ? TagRange{} : TagRange{0, static_cast<TagId>(tree.Count() - 1)};
            }
            if (String problem = CheckPath(parsed.path); !problem.empty()) {
                return std::unexpected(Format("pattern '{}': {}", pattern, problem));
            }
            const Optional<TagId> id = tree.Find(parsed.path);
            if (!id) {
                return std::unexpected(Format("pattern '{}': unknown tag '{}'", pattern, parsed.path));
            }
            return parsed.kind == PatternKind::Subtree ? tree.Range(*id) : TagRange{*id, *id};
        }

    }  // namespace

    std::expected<void, String> TagTree::Add(StringView path) {
        ASSERT(!_baked, "TagTree::Add after Bake");
        if (_baked) {
            return std::unexpected(Format("cannot add the tag '{}': the tree is baked", path));
        }
        if (String problem = CheckPath(path); !problem.empty()) {
            return std::unexpected(Format("cannot add the tag '{}': {}", path, problem));
        }

        // The ancestors are prefixes of the path that end before a dot; the first one that is unknown starts the new nodes.
        Vector<size_t> prefixEnds;
        for (size_t dot = path.find(kSeparator); dot != StringView::npos; dot = path.find(kSeparator, dot + 1)) {
            prefixEnds.push_back(dot);
        }
        prefixEnds.push_back(path.size());

        size_t firstMissing = 0;
        while (firstMissing < prefixEnds.size() && _index.find(path.substr(0, prefixEnds[firstMissing])) != _index.end()) {
            ++firstMissing;
        }
        const size_t missing = prefixEnds.size() - firstMissing;
        if (missing == 0) {
            return {};
        }
        if (Count() + missing > kMaxTagCount) {
            return std::unexpected(Format("cannot add the tag '{}': a tree holds at most {} tags", path, kMaxTagCount));
        }
        size_t newNameBytes = 0;
        for (size_t i = firstMissing; i < prefixEnds.size(); ++i) {
            newNameBytes += prefixEnds[i] + 1;
        }
        if (_nameBytes + newNameBytes > kMaxTagNameBlob) {
            return std::unexpected(Format("cannot add the tag '{}': the names of a tree take at most {} bytes", path, kMaxTagNameBlob));
        }

        uint32_t parent = 0;
        if (firstMissing > 0) {
            parent = _index.find(path.substr(0, prefixEnds[firstMissing - 1]))->second;
        }
        for (size_t i = firstMissing; i < prefixEnds.size(); ++i) {
            const uint32_t node = static_cast<uint32_t>(_nodes.size());
            Node created;
            created.path = String(path.substr(0, prefixEnds[i]));
            created.parent = parent;
            created.depth = _nodes[parent].depth + 1;
            _index.emplace(created.path, node);
            _nodes.push_back(std::move(created));
            _nodes[parent].children.push_back(node);
            parent = node;
        }
        _nameBytes += newNameBytes;
        return {};
    }

    void TagTree::Bake() {
        if (_baked) {
            return;
        }
        _byId.clear();
        _byId.reserve(Count());

        Vector<uint32_t> pending;
        for (auto it = _nodes[0].children.rbegin(); it != _nodes[0].children.rend(); ++it) {
            pending.push_back(*it);
        }
        while (!pending.empty()) {
            const uint32_t node = pending.back();
            pending.pop_back();
            _nodes[node].id = static_cast<TagId>(_byId.size());
            _byId.push_back(node);
            for (auto it = _nodes[node].children.rbegin(); it != _nodes[node].children.rend(); ++it) {
                pending.push_back(*it);
            }
        }

        // A child has a higher id than its parent and the last child ends last, so the ids from the top down see finished children.
        for (size_t i = _byId.size(); i > 0; --i) {
            Node& node = _nodes[_byId[i - 1]];
            node.last = node.children.empty() ? node.id : _nodes[node.children.back()].last;
        }
        _baked = true;
    }

    const TagTree::Node& TagTree::NodeOf(TagId tag) const {
        ASSERT(_baked, "TagTree is asked before Bake");
        ASSERT(tag < _byId.size(), "TagTree: tag id out of range");
        return _nodes[_byId[tag]];
    }

    Optional<TagId> TagTree::Find(StringView path) const {
        ASSERT(_baked, "TagTree::Find before Bake");
        const auto it = _index.find(path);
        if (it == _index.end()) {
            return std::nullopt;
        }
        return _nodes[it->second].id;
    }

    TagId TagTree::Parent(TagId tag) const {
        const uint32_t parent = NodeOf(tag).parent;
        return parent == 0 ? kNoTag : _nodes[parent].id;
    }

    TagRange TagTree::Range(TagId tag) const {
        const Node& node = NodeOf(tag);
        return TagRange{node.id, node.last};
    }

    TagRange TagTree::DescendantRange(TagId tag) const {
        const Node& node = NodeOf(tag);
        return TagRange{static_cast<TagId>(node.id + 1), node.last};
    }

    uint32_t TagTree::Depth(TagId tag) const {
        return NodeOf(tag).depth;
    }

    StringView TagTree::Name(TagId tag) const {
        return NodeOf(tag).path;
    }

    std::expected<TagId, String> TagTree::ResolveTag(StringView path) const {
        return ResolveTagIn(*this, path);
    }

    std::expected<TagRange, String> TagTree::Resolve(StringView pattern) const {
        return ResolveIn(*this, pattern);
    }

    void TagTree::FillBaked(BakedTagTree& out) const {
        ASSERT(_baked, "TagTree::FillBaked before Bake");
        memset(&out, 0, sizeof(BakedTagTree));
        size_t blobUsed = 0;
        for (size_t i = 0; i < _byId.size(); ++i) {
            const Node& node = _nodes[_byId[i]];
            out.parent[i] = node.parent == 0 ? kNoTag : _nodes[node.parent].id;
            out.subtreeEnd[i] = node.last;
            out.nameOffset[i] = static_cast<uint16_t>(blobUsed);
            memcpy(out.names + blobUsed, node.path.data(), node.path.size());
            blobUsed += node.path.size() + 1;  // the NUL is there: the block starts zeroed
        }
        out.count = static_cast<uint16_t>(_byId.size());
        out.nameBlobSize = static_cast<uint16_t>(blobUsed);
        ASSERT(blobUsed == _nameBytes, "TagTree: the name blob does not match the counted bytes");
    }

    Optional<TagId> BakedTagTree::Find(StringView path) const {
        for (TagId tag = 0; tag < count; ++tag) {
            if (Name(tag) == path) {
                return tag;
            }
        }
        return std::nullopt;
    }

    TagId BakedTagTree::Parent(TagId tag) const {
        return tag < count ? parent[tag] : kNoTag;
    }

    TagRange BakedTagTree::Range(TagId tag) const {
        return tag < count ? TagRange{tag, subtreeEnd[tag]} : TagRange{};
    }

    TagRange BakedTagTree::DescendantRange(TagId tag) const {
        return tag < count ? TagRange{static_cast<TagId>(tag + 1), subtreeEnd[tag]} : TagRange{};
    }

    uint32_t BakedTagTree::Depth(TagId tag) const {
        uint32_t depth = 0;
        for (TagId id = tag; id < count; id = parent[id]) {
            ++depth;
        }
        return depth;
    }

    StringView BakedTagTree::Name(TagId tag) const {
        if (tag >= count) {
            return {};
        }
        const size_t end = tag + 1 < count ? nameOffset[tag + 1] : nameBlobSize;
        return StringView{names + nameOffset[tag], end - 1 - nameOffset[tag]};
    }

    std::expected<TagId, String> BakedTagTree::ResolveTag(StringView path) const {
        return ResolveTagIn(*this, path);
    }

    std::expected<TagRange, String> BakedTagTree::Resolve(StringView pattern) const {
        return ResolveIn(*this, pattern);
    }

}  // namespace BECore

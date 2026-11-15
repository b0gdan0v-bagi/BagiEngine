#include "TagTree.h"


namespace BECore {

    namespace {

        constexpr char kSeparator = '.';

        bool IsValidSegmentCharacter(char c) {
            return c != '*' && c != ' ' && c != '\t' && c != '\n' && c != '\r';
        }

        /// The first problem of a path as a sentence, empty when the path is a valid tag.
        String CheckPath(StringView path) {
            if (path.empty()) {
                return "the tag path is empty";
            }
            size_t segmentStart = 0;
            while (true) {
                const size_t dot = path.find(kSeparator, segmentStart);
                const size_t segmentEnd = dot == StringView::npos ? path.size() : dot;
                if (segmentEnd == segmentStart) {
                    return Format("the tag path '{}' has an empty segment", path);
                }
                for (size_t i = segmentStart; i < segmentEnd; ++i) {
                    if (!IsValidSegmentCharacter(path[i])) {
                        return Format("the tag path '{}' has a '*' or a whitespace in a segment", path);
                    }
                }
                if (dot == StringView::npos) {
                    return {};
                }
                segmentStart = dot + 1;
            }
        }

    }  // namespace

    std::expected<void, String> TagTree::Add(StringView path) {
        ASSERT(!_baked, "TagTree::Add after Bake");
        if (_baked) {
            return std::unexpected(Format("cannot add the tag '{}': the tree is baked", path));
        }
        if (String problem = CheckPath(path); !problem.empty()) {
            return std::unexpected(std::move(problem));
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

        uint32_t parent = 0;
        if (firstMissing > 0) {
            parent = _index.find(path.substr(0, prefixEnds[firstMissing - 1]))->second;
        }
        for (size_t i = firstMissing; i < prefixEnds.size(); ++i) {
            const uint32_t node = static_cast<uint32_t>(_nodes.size());
            Node created;
            created.path = String(path.substr(0, prefixEnds[i]));
            created.depth = _nodes[parent].depth + 1;
            _index.emplace(created.path, node);
            _nodes.push_back(std::move(created));
            _nodes[parent].children.push_back(node);
            parent = node;
        }
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

}  // namespace BECore

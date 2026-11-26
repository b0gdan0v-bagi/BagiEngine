#include "SceneNodePath.h"

#include <BECore/Scene/SceneNode.h>

namespace BECore {

    namespace {

        constexpr char kSeparator = '/';
        constexpr char kEscape = '\\';
        constexpr char kIndexOpen = '[';
        constexpr char kIndexClose = ']';
        constexpr uint32_t kDecimalBase = 10;

        constexpr bool IsEscaped(char symbol) {
            return symbol == kSeparator || symbol == kEscape || symbol == kIndexOpen;
        }

        /// One segment of a path: the name with its escapes resolved and which of the children of that name is meant
        struct Segment {
            String name;
            uint32_t sibling = 0;
        };

        /// Cuts a trailing `[digits]` off `segment.name` into `segment.sibling`; @p open is the place of its last unescaped `[`. False when the number is too large
        bool TakeSiblingIndex(Segment& segment, size_t open) {
            const size_t close = segment.name.size() - 1;
            if (open + 1 >= close || segment.name[close] != kIndexClose) {
                return true;
            }
            uint64_t number = 0;
            for (size_t i = open + 1; i < close; ++i) {
                const char symbol = segment.name[i];
                if (symbol < '0' || symbol > '9') {
                    return true;  // brackets that are part of the name
                }
                number = number * kDecimalBase + static_cast<uint64_t>(symbol - '0');
                if (number > std::numeric_limits<uint32_t>::max()) {
                    return false;
                }
            }
            segment.sibling = static_cast<uint32_t>(number);
            segment.name.resize(open);
            return true;
        }

        /// Reads the segment that starts at @p position of @p path and leaves @p position at the `/` that ends it, or at the end
        std::expected<void, StringView> ReadSegment(StringView path, size_t& position, Segment& segment) {
            segment.name.clear();
            segment.sibling = 0;
            Optional<size_t> open;
            for (; position < path.size() && path[position] != kSeparator; ++position) {
                char symbol = path[position];
                if (symbol == kEscape) {
                    if (position + 1 >= path.size() || !IsEscaped(path[position + 1])) {
                        return std::unexpected(StringView(R"(a '\' of a node path escapes '/', '[' or '\' only)"));
                    }
                    symbol = path[++position];
                } else if (symbol == kIndexOpen) {
                    open = segment.name.size();
                }
                segment.name.push_back(symbol);
            }
            if (open && !TakeSiblingIndex(segment, *open)) {
                return std::unexpected(StringView("an index of a node path is too large"));
            }
            return {};
        }

        SceneNode* FindChild(SceneNode& parent, const Segment& segment) {
            uint32_t left = segment.sibling;
            for (const auto& child : parent.GetChildren()) {
                if (child->GetName().ToStringView() == StringView(segment.name) && left-- == 0) {
                    return child.Get();
                }
            }
            return nullptr;
        }

    }  // namespace

    std::expected<SceneNode*, StringView> FindSceneNodeByPath(SceneNode& root, StringView path) {
        if (path.size() > SceneNodePathLimits::kMaxPathBytes) {
            return std::unexpected(StringView("the node path is too long"));
        }
        SceneNode* node = &root;
        Segment segment;
        size_t position = 0;
        // The whole text is read even past a node that is not there, so that a bad path is told from a missing node
        for (size_t count = 1;; ++count, ++position) {
            if (count > SceneNodePathLimits::kMaxSegments) {
                return std::unexpected(StringView("the node path has too many segments"));
            }
            if (const auto read = ReadSegment(path, position, segment); !read) {
                return std::unexpected(read.error());
            }
            if (node != nullptr) {
                node = FindChild(*node, segment);
            }
            if (position >= path.size()) {
                return node;
            }
        }
    }

    void AppendSceneNodeSegment(String& path, const SceneNode& node) {
        const StringView name = node.GetName().ToStringView();
        for (const char symbol : name) {
            if (IsEscaped(symbol)) {
                path.push_back(kEscape);
            }
            path.push_back(symbol);
        }
        const SceneNode* parent = node.GetParent();
        if (parent == nullptr) {
            return;
        }
        uint32_t earlier = 0;
        for (const auto& sibling : parent->GetChildren()) {
            if (sibling.Get() == &node) {
                break;
            }
            if (sibling->GetName() == node.GetName()) {
                ++earlier;
            }
        }
        if (earlier > 0) {
            AppendFormat(path, "[{}]", earlier);
        }
    }

    void AppendSceneNodePath(String& path, const SceneNode& node) {
        const SceneNode* parent = node.GetParent();
        if (parent == nullptr) {
            return;
        }
        if (parent->GetParent() != nullptr) {
            AppendSceneNodePath(path, *parent);
            path.push_back(kSeparator);
        }
        AppendSceneNodeSegment(path, node);
    }

}  // namespace BECore

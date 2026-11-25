#pragma once

#include <cstddef>

namespace BECore::PanelAssetLimits {

    // A panel asset is untrusted text that is read again at every hot reload. The loader refuses what is beyond these limits with an
    // issue, so a file cannot make the editor walk or hold more than a panel ever needs. The size of the file is the reader's
    // (`ReaderLimits::kMaxXmlDocumentBytes`).

    /// Levels of elements in the tree: a root element is level 1
    inline constexpr size_t kMaxDepth = 16;

    /// Elements of the whole asset, columns not counted
    inline constexpr size_t kMaxElements = 1024;

    /// Columns of all tables of the asset together
    inline constexpr size_t kMaxColumns = 128;

    /// Sources of one asset
    inline constexpr size_t kMaxSources = 32;

    /// Bytes of one text field (a title, a text, a binding, a command)
    inline constexpr size_t kMaxTextBytes = 512;

    /// Bytes of an `id`: 1 to this many of `[A-Za-z0-9_.-]`
    inline constexpr size_t kMinIdBytes = 1;
    inline constexpr size_t kMaxIdBytes = 64;

    /// Bytes of one issue's text: issues reach the panel and the log
    inline constexpr size_t kMaxIssueBytes = 256;

}  // namespace BECore::PanelAssetLimits

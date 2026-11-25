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

    /// Lines of a scrolling text (`PanelText::maxLines`) and rows of a scrolling table (`PanelTable::scrollRows`): the height of the region a file may ask for
    inline constexpr size_t kMaxScrollLines = 64;
    inline constexpr size_t kMaxScrollRows = 64;

    /// em of a child region's `width` and `height`: what a file may ask for
    inline constexpr float kMaxChildExtent = 256.0f;

    /// What a number control may be asked for: `min`, `max` and `step` of a `PanelNumber` lie within plus and minus this
    inline constexpr float kMaxNumberExtent = 1.0e9f;

    /// Popups of one asset; a popup is modal and none holds another
    inline constexpr size_t kMaxPopups = 8;

    /// Bytes of one text field (a title, a text, a binding, a command)
    inline constexpr size_t kMaxTextBytes = 512;

    /// Terms of one condition (`visible`, `enableWhen`, `open`): a few options joined by `and` / `or`
    inline constexpr size_t kMaxConditionTerms = 8;

    /// Bytes of an `id`: 1 to this many of `[A-Za-z0-9_.-]`
    inline constexpr size_t kMinIdBytes = 1;
    inline constexpr size_t kMaxIdBytes = 64;

    /// Bytes of one issue (where it is and what is wrong together): issues reach the panel and the log
    inline constexpr size_t kMaxIssueWhereBytes = 96;
    inline constexpr size_t kMaxIssueMessageBytes = 160;

    /// Issues of one load; the last place says that the rest were dropped, as the reader's list of errors does
    inline constexpr size_t kMaxIssues = 64;

}  // namespace BECore::PanelAssetLimits

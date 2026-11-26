#pragma once

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /// The name of @p part in an asset file (`sim`, `visual`, `editor`); empty for `Common`, which is no part.
    constexpr StringView PartName(AssetPurpose part) {
        return part == AssetPurpose::Common ? StringView{} : PurposeName(part);
    }

    /**
     * @brief The virtual path of the split file of @p part: `gem.asset.xml` gives `gem.sim.xml`
     *
     * A pure transformation of the path string. Empty when @p assetPath does not end in `.asset.xml` or @p part is `Common`.
     */
    String SplitPartPath(StringView assetPath, AssetPurpose part);

    /**
     * @brief Open the content of one part of the asset @p assetPath, with the purpose filter set to @p part
     *
     * The split file of the part (`SplitPartPath`) is used only from the real folder of the whole file that wins the content
     * layers and only when its write time is strictly later; when no layer has the whole file, the split file the layers
     * resolve is used. Its root is the part element, so the object of the part is the first child. Otherwise the whole file
     * is opened (XML or binary) and the reader is entered into the part element. Either way the reader never visits another
     * part, and every reflected object read through it is checked against the filter.
     *
     * @return The reader positioned inside the part, a null pointer when the whole file has no such part, or a message
     *         when no file of the asset opens or @p part is `Common`.
     */
    std::expected<UniquePtr<IDeserializer>, String> OpenAssetPart(StringView assetPath, AssetPurpose part);

    /// The parts of one whole asset file as documents, empty where the file has no such part
    struct AssetPartTexts {
        Optional<String> sim;
        Optional<String> visual;
        Optional<String> editor;

        const Optional<String>& Get(AssetPurpose part) const;
    };

    /**
     * @brief Cut a whole asset document into one document per part present
     *
     * The root of each document is the part element, so it is what the loader reads from a split file. Pure text work
     * (pugixml only): no engine service, no file. Comments and declarations of the whole file are not carried over.
     *
     * @return The texts, or a message when the text does not parse, a child of the root is no part, a part is repeated
     *         or there is no part at all.
     */
    std::expected<AssetPartTexts, String> SplitAssetXml(StringView xml);

    /**
     * @brief Write the split files of the whole asset @p virtualAssetPath (the packer step)
     *
     * The files are named by `SplitPartPath` and land next to the source, or in @p outputVirtualFolder (same file names)
     * when it is given; the folder is created inside its mount point when it does not exist yet. A part that is gone from
     * the source has its old split file removed, so the files mirror the source. Nothing is touched when the source does
     * not split. `OpenAssetPart` prefers the files only beside the source and with a later write time; files in another
     * output folder serve only where no whole file resolves.
     *
     * @return false (and an error in the log) when the source does not read or split or a file cannot be written.
     */
    bool SplitAssetFile(StringView virtualAssetPath, StringView outputVirtualFolder = {});

}  // namespace BECore

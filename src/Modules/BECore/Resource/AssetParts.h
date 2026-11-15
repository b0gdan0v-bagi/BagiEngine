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
     * The split file of the part (`SplitPartPath`) is used when it resolves and is not older than the whole file (or the
     * whole file is missing): its root is the part element, so the object of the part is the first child. Otherwise the
     * whole file is opened (XML or binary) and the reader is entered into the part element. Either way the reader never
     * visits another part, and every reflected object read through it is checked against the filter.
     *
     * @return The reader positioned inside the part, a null pointer when the whole file has no such part, or a message
     *         when no file of the asset opens or @p part is `Common`.
     */
    std::expected<UniquePtr<IDeserializer>, String> OpenAssetPart(StringView assetPath, AssetPurpose part);

}  // namespace BECore

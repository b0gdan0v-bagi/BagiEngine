#pragma once

#include <BECore/Math/Vec2.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/AssetKind.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    class Scene;
    class SceneNode;

    /// A content file dropped into a scene
    struct SceneAssetDrop {
        AssetKind kind = AssetKind::None;
        PoolString path;   ///< virtual, such as `assets/pieces/rook.png`
        Vec2 position;     ///< scene units: where the node is centred
        Vec2 textureSize;  ///< the pixels of a dropped texture; unused for another kind
    };

    /// The kinds a scene makes a node of: `Texture`, `Font`, `Sound`, `Node`
    [[nodiscard]] bool MakesSceneNode(AssetKind kind);

    /**
     * @brief Adds an authored top-level node for @p drop to @p scene, so a Save writes it
     *
     * A texture, a font or a sound becomes a node named after the file with the components that show or play it (a name a sibling has
     * gets a number). A node file (`config/<ref>.xml`) becomes an instance of it (`Scene::InstantiateNode`) under the file's own name, so a
     * Save writes `<node ref>`; its `TransformComponent`, when it has one, is centred on the position.
     *
     * Needs no ImGui; a node file is read only through @p loader.
     *
     * @return the new node, which @p scene holds; else why nothing was added: a kind that makes no node, a texture with no size (one that
     *         did not load), a node file that is no valid reference or cannot be loaded
     */
    [[nodiscard]] std::expected<SceneNode*, String> AddSceneNodeOfAsset(Scene& scene, const SceneAssetDrop& drop, NodeFileLoaderRef loader);

}  // namespace BECore

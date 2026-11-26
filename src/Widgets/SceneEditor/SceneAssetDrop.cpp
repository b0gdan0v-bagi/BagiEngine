#include "SceneAssetDrop.h"

#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/File/Path.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/TextComponent.h>

namespace BECore {

    namespace {

        constexpr StringView kNodeFileExtension = ".xml";
        // The box of the text a dropped font makes, in scene units: room for a file name at the default font size
        constexpr float kTextBoxWidth = 240.0f;
        constexpr float kTextBoxHeight = 40.0f;

        /// The reference of the node file @p path (`config/nodes/X.xml` gives `nodes/X`), empty for a path that is no `.xml` under `config`
        StringView NodeRefOf(StringView path) {
            const StringView mount = MountPoints::config.ToPoolString().ToStringView();
            if (!PathIsUnder(path, mount) || PathExtension(path) != kNodeFileExtension) {
                return {};
            }
            return path.substr(mount.size() + 1, path.size() - mount.size() - 1 - kNodeFileExtension.size());
        }

        void Centre(TransformComponent& transform, Vec2 position) {
            transform._x = position.x - transform._width * 0.5f;
            transform._y = position.y - transform._height * 0.5f;
        }

        IntrusivePtr<TransformComponent> MakeTransform(Vec2 position, Vec2 size) {
            IntrusivePtr<TransformComponent> transform = New<TransformComponent>();
            transform->_width = size.x;
            transform->_height = size.y;
            Centre(*transform, position);
            return transform;
        }

        /// An empty authored node at the top of @p scene, named after the file
        IntrusivePtr<SceneNode> MakeNode(Scene& scene, StringView path) {
            const PoolString name = scene.MakeUniqueSiblingName(scene.GetRootNode(), PathStem(path));
            IntrusivePtr<SceneNode> node = scene.CreateNode(name);
            node->SetGenerated(false);
            return node;
        }

        std::expected<SceneNode*, String> AddInstance(Scene& scene, const SceneAssetDrop& drop, NodeFileLoaderRef loader) {
            const StringView ref = NodeRefOf(drop.path.ToStringView());
            if (ref.empty()) {
                return std::unexpected(Format("'{}' is not a node file under config", drop.path.CStr()));
            }
            pugi::xml_document reference;
            reference.append_child(kNodeElementName.data()).append_attribute(kNodeRefAttribute.data()).set_value(String(ref).c_str());
            // Expanded here first: the reason reaches the caller, and InstantiateNode has nothing left to refuse and log
            pugi::xml_document expanded;
            String error;
            if (!ExpandNodeReference(reference.document_element(), loader, true, expanded, error)) {
                return std::unexpected(Format("'{}': {}", drop.path.CStr(), error));
            }
            const IntrusivePtr<SceneNode> node = scene.InstantiateNode(reference.document_element(), loader);
            if (!node) {
                return std::unexpected(Format("'{}' cannot be instantiated", drop.path.CStr()));
            }
            node->SetGenerated(false);
            if (const IntrusivePtr<TransformComponent> transform = node->GetComponent<TransformComponent>()) {
                Centre(*transform, drop.position);
            }
            return node.Get();
        }

    }  // namespace

    bool MakesSceneNode(AssetKind kind) {
        switch (kind) {
            case AssetKind::Texture:
            case AssetKind::Font:
            case AssetKind::Sound:
            case AssetKind::Node:
                return true;
            case AssetKind::None:
            case AssetKind::Scene:
            case AssetKind::UiDocument:
            case AssetKind::Config:
            case AssetKind::Other:
                break;
        }
        return false;
    }

    std::expected<SceneNode*, String> AddSceneNodeOfAsset(Scene& scene, const SceneAssetDrop& drop, NodeFileLoaderRef loader) {
        if (!MakesSceneNode(drop.kind)) {
            return std::unexpected(Format("'{}' makes no node", drop.path.CStr()));
        }
        if (drop.kind == AssetKind::Node) {
            return AddInstance(scene, drop, loader);
        }
        if (drop.kind == AssetKind::Texture && (drop.textureSize.x <= 0.0f || drop.textureSize.y <= 0.0f)) {
            return std::unexpected(Format("'{}' did not load as a picture", drop.path.CStr()));
        }

        const StringView path = drop.path.ToStringView();
        const IntrusivePtr<SceneNode> node = MakeNode(scene, path);
        // The fields are set before AddComponent: attaching is where a component loads its file
        if (drop.kind == AssetKind::Texture) {
            node->AddComponent(MakeTransform(drop.position, drop.textureSize));
            IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
            sprite->_texturePath = drop.path;
            node->AddComponent(std::move(sprite));
        } else if (drop.kind == AssetKind::Font) {
            node->AddComponent(MakeTransform(drop.position, Vec2{kTextBoxWidth, kTextBoxHeight}));
            IntrusivePtr<TextComponent> text = New<TextComponent>();
            text->_text = String(PathStem(path));
            text->_fontPath = drop.path;
            node->AddComponent(std::move(text));
        } else {
            IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
            audio->_clip = drop.path;
            node->AddComponent(std::move(audio));
        }
        return node.Get();
    }

}  // namespace BECore

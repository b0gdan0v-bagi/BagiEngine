#include "ComponentCategories.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Games/GameEditorTools.h>
#include <Games/GameManager.h>

namespace BECore {

    namespace {

        struct BuiltInCategory {
            StringView name;
            Color color;
        };

        // Bright enough to read on the dark surfaces of the theme, and away from its violet accent, which marks a node with no category
        constexpr BuiltInCategory kCategories[] = {
            {"ui", Color(118, 210, 120, 255)}, {"render", Color(96, 165, 250, 255)}, {"animation", Color(236, 120, 190, 255)}, {"audio", Color(242, 150, 72, 255)}, {"logic", Color(232, 208, 88, 255)},
        };

        constexpr bool MatchesBuiltInNames() {
            if (std::size(kCategories) != std::size(kBuiltInComponentCategories)) {
                return false;
            }
            for (size_t i = 0; i < std::size(kCategories); ++i) {
                if (kCategories[i].name != kBuiltInComponentCategories[i]) {
                    return false;
                }
            }
            return true;
        }
        static_assert(MatchesBuiltInNames(), "ComponentCategories.cpp: one row per name of kBuiltInComponentCategories, in its order");

        /// Unknown names already reported. Views of the literals in the generated ClassMeta, so no string is copied
        Vector<StringView>& WarnedNames() {
            static Vector<StringView> names;
            return names;
        }

    }  // namespace

    Optional<Color> CategoryColor(const ClassMeta& meta) {
        if (meta.category.empty()) {
            return {};
        }
        for (const BuiltInCategory& category : kCategories) {
            if (category.name == meta.category) {
                return category.color;
            }
        }
        if (GameManager::IsInstanceCreated()) {
            if (const GameToolCategory* category = GameManager::GetInstance().GetEditorTools().FindCategory(meta.category)) {
                return category->color;
            }
        }
        auto& warned = WarnedNames();
        if (eastl::find(warned.begin(), warned.end(), meta.category) == warned.end()) {
            warned.push_back(meta.category);
            LOG_WARNING("Editor: class '{}' names the category '{}', which is not built in and which the active game does not declare", meta.typeName, meta.category);
        }
        return {};
    }

    const ClassMeta* CategoryOfNode(const SceneNode& node) {
        for (const auto& component : node.GetComponents()) {
            const ClassMeta& meta = component->GetTypeMeta();
            if (!meta.category.empty()) {
                return &meta;
            }
        }
        return nullptr;
    }

    Optional<Color> CategoryColorOfNode(const SceneNode& node) {
        const ClassMeta* meta = CategoryOfNode(node);
        return meta != nullptr ? CategoryColor(*meta) : Optional<Color>();
    }

}  // namespace BECore

#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/UI/UIVariantCondition.h>

namespace BECore {

    /**
     * @brief An alternative view size of a scene, used while `when` holds for the render output.
     *
     * In a file: `<viewVariants><view name="Portrait" when="orientation:portrait" width="720" height="1280"/></viewVariants>`.
     * `when` is the language of UIVariantCondition, tested against the rect `{0, 0, outputWidth, outputHeight}` in output pixels.
     * `name` is optional and only for the game, the logs and the tools.
     */
    struct SceneViewVariant {
        PoolString name;
        UIVariantCondition when;
        float width = 0.0f;
        float height = 0.0f;

        void Serialize(ISerializer& s) const;

        /// @return false when `when` was malformed (one reported error): the list drops that variant
        [[nodiscard]] bool Deserialize(IDeserializer& d);

        [[nodiscard]] bool operator==(const SceneViewVariant&) const = default;
    };

    /// The view a scene draws with: the default one (`index` -1, empty `name`) or a variant (`index` in the list as loaded)
    struct ActiveSceneView {
        PoolString name;
        float width = 0.0f;
        float height = 0.0f;
        int32_t index = -1;

        [[nodiscard]] bool operator==(const ActiveSceneView&) const = default;
    };

    namespace Ser {

        /// A list of view variants is `<viewVariants><view .../>...</viewVariants>` in a file; an empty list writes nothing
        template <>
        struct DataAccessor<eastl::vector<SceneViewVariant>> {
            static void Save(ISerializer& s, const eastl::vector<SceneViewVariant>& variants, eastl::string_view key);
            static bool Load(IDeserializer& d, eastl::vector<SceneViewVariant>& variants, eastl::string_view key);
        };

    }  // namespace Ser

    namespace PropVisit {

        /// The inspector does not edit view variants: they are shown as nothing (the same as the variants of a canvas)
        inline bool Visit(IPropertyVisitor& /*v*/, eastl::vector<SceneViewVariant>& /*variants*/, eastl::string_view /*key*/) {
            return false;
        }

    }  // namespace PropVisit

    class SceneViewSelector {
    public:
        /**
         * @brief The view a scene shows in an output of outputWidth x outputHeight pixels. Pure: no scene, renderer or window.
         *
         * The first variant whose `when` holds for `{0, 0, outputWidth, outputHeight}` and whose size is positive and finite wins; else the
         * default size with index -1. An output that is not positive (no renderer, a minimized window) has no shape, so it is the default too.
         */
        [[nodiscard]] static ActiveSceneView Pick(float defaultWidth, float defaultHeight, eastl::span<const SceneViewVariant> variants, float outputWidth, float outputHeight);
    };

}  // namespace BECore

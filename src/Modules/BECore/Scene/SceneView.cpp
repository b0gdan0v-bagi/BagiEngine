#include "SceneView.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr StringView ViewItem = "view";

        bool IsUsableSize(float width, float height) {
            return std::isfinite(width) && std::isfinite(height) && width > 0.0f && height > 0.0f;
        }

    }  // namespace

    void SceneViewVariant::Serialize(ISerializer& s) const {
        if (!name.Empty()) {
            Ser::DataAccessor<PoolString>::Save(s, name, "name");
        }
        const String text = when.ToText();
        s.WriteAttribute("when", StringView(text));
        Ser::DataAccessor<float>::Save(s, width, "width");
        Ser::DataAccessor<float>::Save(s, height, "height");
    }

    bool SceneViewVariant::Deserialize(IDeserializer& d) {
        Ser::Load(d, name, "name");
        Ser::Load(d, width, "width");
        Ser::Load(d, height, "height");
        String text;
        if (d.ReadAttribute("when", text)) {
            const auto condition = UIVariantCondition::FromText(StringView(text));
            if (!condition) {
                d.ReportError("when", StringView(condition.error().c_str()));
                return false;
            }
            when = *condition;
        }
        return true;
    }

    void Ser::DataAccessor<eastl::vector<SceneViewVariant>>::Save(ISerializer& s, const eastl::vector<SceneViewVariant>& variants, StringView key) {
        if (variants.empty()) {
            return;
        }
        size_t count = variants.size();
        if (s.BeginArray(key, ViewItem, count)) {
            for (const SceneViewVariant& variant : variants) {
                if (s.BeginArrayElement()) {
                    variant.Serialize(s);
                    s.EndArrayElement();
                }
            }
            s.EndArray();
        }
    }

    bool Ser::DataAccessor<eastl::vector<SceneViewVariant>>::Load(IDeserializer& d, eastl::vector<SceneViewVariant>& variants, StringView key) {
        size_t count = 0;
        if (!d.BeginArray(key, ViewItem, count)) {
            return false;
        }
        variants.clear();
        for (size_t i = 0; i < count; ++i) {
            if (d.BeginArrayElement()) {
                SceneViewVariant variant;
                if (variant.Deserialize(d)) {
                    variants.push_back(eastl::move(variant));
                }
                d.EndArrayElement();
            }
        }
        d.EndArray();
        return true;
    }

    ActiveSceneView SceneViewSelector::Pick(float defaultWidth, float defaultHeight, eastl::span<const SceneViewVariant> variants, float outputWidth, float outputHeight) {
        const ActiveSceneView fallback{PoolString{}, defaultWidth, defaultHeight, -1};
        if (!IsUsableSize(outputWidth, outputHeight)) {
            return fallback;
        }
        const Rect output{0.0f, 0.0f, outputWidth, outputHeight};
        for (size_t i = 0; i < variants.size(); ++i) {
            const SceneViewVariant& variant = variants[i];
            if (IsUsableSize(variant.width, variant.height) && variant.when.Matches(output)) {
                return {variant.name, variant.width, variant.height, static_cast<int32_t>(i)};
            }
        }
        return fallback;
    }

}  // namespace BECore

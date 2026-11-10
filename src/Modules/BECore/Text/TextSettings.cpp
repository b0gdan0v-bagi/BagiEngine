#include "TextSettings.h"

namespace BECore {

    namespace {

        constexpr uint32_t kMinPageSize = 64;
        constexpr uint32_t kMaxPageSize = 8192;

        constexpr int kMinWeight = 100;
        constexpr int kMaxWeight = 900;
        constexpr int kMaxFaceWeight = 1000;
        constexpr int kRegularWeight = 400;
        constexpr int kMediumWeight = 500;
        constexpr int kRankStride = 1024;  // above any weight distance: the group of a face outranks its distance

        /// The CSS font matching order as one number, the lowest wins: the group (which side of the asked weight, in
        /// which order) times a stride plus the distance inside the group
        int FaceRank(int weight, int faceWeight) {
            if (weight < kRegularWeight) {
                return faceWeight <= weight ? weight - faceWeight : kRankStride + (faceWeight - weight);
            }
            if (weight > kMediumWeight) {
                return faceWeight >= weight ? faceWeight - weight : kRankStride + (weight - faceWeight);
            }
            if (faceWeight >= weight && faceWeight <= kMediumWeight) {
                return faceWeight - weight;
            }
            if (faceWeight < weight) {
                return kRankStride + (weight - faceWeight);
            }
            return 2 * kRankStride + (faceWeight - weight);
        }

        const FontFace* PickFace(const FontFamily& family, int weight) {
            const FontFace* best = nullptr;
            int bestRank = 0;
            for (const FontFace& face : family.faces) {
                const int rank = FaceRank(weight, face.weight);
                if (!best || rank < bestRank) {
                    best = &face;
                    bestRank = rank;
                }
            }
            return best;
        }

        FontFamily ParseFamily(const XmlNode& node) {
            FontFamily family;
            const auto name = node.ParseAttribute<eastl::string_view>("name");
            if (!name || name->empty()) {
                LOG_WARNING("TextConfig: a family needs a name, ignoring it");
                return family;
            }
            family.name = PoolString::Intern(*name);
            for (XmlNode face : node.Children()) {
                const auto weight = face.ParseAttribute<int>("weight");
                const auto path = face.ParseAttribute<eastl::string_view>("path");
                if (!weight || *weight < 1 || *weight > kMaxFaceWeight || !path || path->empty()) {
                    LOG_WARNING("TextConfig: a face of the family '{}' needs a weight of 1..1000 and a path, ignoring it"_format(*name));
                    continue;
                }
                family.faces.push_back({*weight, PoolString::Intern(*path)});
            }
            if (family.faces.empty()) {
                LOG_WARNING("TextConfig: the family '{}' has no valid face, ignoring it"_format(*name));
                family.name = PoolString{};
            }
            return family;
        }

    }  // namespace

    TextSettings TextSettings::Default() {
        TextSettings settings;
        settings.preload = {
            {0x0020, 0x007E},  // Basic Latin
            {0x00A0, 0x00FF},  // Latin-1 Supplement
            {0x0400, 0x04FF},  // Cyrillic
            {0x2010, 0x2027},  // dashes, quotes, bullets, the ellipsis
            {0x2116, 0x2116},  // numero sign
        };
        return settings;
    }

    PoolString TextSettings::ResolveFontPath(PoolString family, int weight, bool* known) const {
        if (known) {
            *known = true;
        }
        const bool asked = !family.Empty();
        const PoolString name = asked ? family : defaultFamily;
        for (const FontFamily& candidate : families) {
            if (candidate.name == name) {
                if (const FontFace* face = PickFace(candidate, std::clamp(weight, kMinWeight, kMaxWeight))) {
                    return face->path;
                }
            }
        }
        if (asked && known) {
            *known = false;
        }
        return defaultFont;
    }

    TextSettings TextSettings::FromConfig(const XmlNode& node) {
        TextSettings settings = Default();
        if (!node) {
            return settings;
        }
        if (auto font = node.ParseAttribute<eastl::string_view>("defaultFont")) {
            settings.defaultFont = PoolString::Intern(*font);
        }
        if (auto family = node.ParseAttribute<eastl::string_view>("defaultFamily")) {
            settings.defaultFamily = PoolString::Intern(*family);
        }
        if (auto pageSize = node.ParseAttribute<uint32_t>("pageSize")) {
            settings.pageSize = std::clamp(*pageSize, kMinPageSize, kMaxPageSize);
        }
        if (XmlNode families = node.GetChild("families")) {
            for (XmlNode child : families.Children()) {
                FontFamily family = ParseFamily(child);
                if (!family.name.Empty()) {
                    settings.families.push_back(eastl::move(family));
                }
            }
        }
        if (XmlNode preload = node.GetChild("preload")) {
            settings.preload.clear();
            for (XmlNode range : preload.Children()) {
                const auto first = range.ParseAttribute<uint32_t>("from");
                const auto last = range.ParseAttribute<uint32_t>("to");
                if (!first || !last || *first > *last) {
                    LOG_ERROR("TextConfig: a preload range needs from <= to, ignoring it");
                    continue;
                }
                settings.preload.push_back({static_cast<char32_t>(*first), static_cast<char32_t>(*last)});
            }
        }
        return settings;
    }

}  // namespace BECore

#pragma once


namespace BECore {

    /**
     * @brief The named rectangles a game publishes for one canvas, in canvas units (for a SceneView canvas: scene units).
     *
     * An element that names one (`UIRectSpec::region`) is laid out inside it instead of inside its parent. The set is plain data: the canvas layout reads it,
     * the validator and `ui_tree` list it. A canvas has tens of regions at most, so the entries are a flat vector searched linearly.
     */
    class UIRegionSet {
    public:
        struct Entry {
            PoolString name;
            Rect rect;
        };

        /// Publishes @p rect under @p name, replacing the rect of a region that has the name. An empty name is ignored.
        void Set(PoolString name, const Rect& rect);

        /// @return whether a region of that name was there
        bool Remove(PoolString name);

        void Clear();

        /// @return the rect of the region, or nullptr when it is not published (an empty name never is)
        [[nodiscard]] const Rect* Find(PoolString name) const;

        [[nodiscard]] bool IsEmpty() const {
            return _entries.empty();
        }
        [[nodiscard]] size_t Size() const {
            return _entries.size();
        }

        /// In the order of publishing (a replaced region keeps its place)
        [[nodiscard]] auto begin() const {
            return _entries.begin();
        }
        [[nodiscard]] auto end() const {
            return _entries.end();
        }

        /// The same regions with the same rects, whatever the order they were published in (the layout cache compares sets)
        [[nodiscard]] bool operator==(const UIRegionSet& other) const;

    private:
        eastl::vector<Entry> _entries;
    };

}  // namespace BECore

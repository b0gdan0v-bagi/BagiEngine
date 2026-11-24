#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    CORE_ENUM(ArtGenInputKind, uint8_t, None, Bool, Integer, Number, Text, Other)

    /**
     * @brief The value of a workflow input as the Generate form edits it: a default the service reported, a choice of a combo, an override a preset
     * keeps in `params`.
     *
     * Typed, not a JSON text, because the form reads it every frame (`ArtGenWidget::DrawInputField`) and a frame allocates nothing. A value the form
     * has no field for (an array, an object, an integer past 64 bits) is kept as its JSON text in `text`, compact with the keys in byte order, so a
     * preset that holds one is written back as it was.
     */
    struct ArtGenInputValue {
        ArtGenInputKind kind = ArtGenInputKind::None;
        bool flag = false;
        int64_t integer = 0;
        double number = 0.0;
        String text;  // the string for `Text`, the JSON text for `Other`

        [[nodiscard]] static ArtGenInputValue FromBool(bool value);
        [[nodiscard]] static ArtGenInputValue FromInteger(int64_t value);
        [[nodiscard]] static ArtGenInputValue FromNumber(double value);
        [[nodiscard]] static ArtGenInputValue FromText(StringView value);

        /// What @p value holds; a view that is not valid or `null` is `None`
        [[nodiscard]] static ArtGenInputValue Read(JsonView value);
        /// One JSON value: `None` is `null`
        void Write(JsonWriter& writer) const;

        /// As the form shows it in a field: a string bare, an integer in digits, a number as `%g`, a bool as a word; empty for any other kind. Fits
        /// @p size (a terminating NUL included) by cutting
        void ToText(char* out, size_t size) const;

        /// As JsonEquals judges: `1` equals `1.0`, `Other` values are equal as JSON, values of other
        /// kinds are not
        [[nodiscard]] bool operator==(const ArtGenInputValue& other) const;

        /// `operator==` that never parses: two `Other` values compare as the compact sorted text `Read` stores, so `[1]` and `[1.0]` differ. For what
        /// the panel compares every frame (the selected choice of an open combo)
        [[nodiscard]] bool SameStored(const ArtGenInputValue& other) const;
    };

}  // namespace BECore

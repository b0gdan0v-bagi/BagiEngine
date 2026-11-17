#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>

namespace BECore::Tests {

    namespace {

        // A `requires` outside a template is a hard error, so the question goes through a type parameter
        template <typename Text>
        concept CanIntern = requires(Text && text) {
            PoolString::Intern(static_cast<Text&&>(text));
        };

        template <typename Text>
        concept CanFind = requires(Text && text) {
            PoolString::Find(static_cast<Text&&>(text));
        };

    }  // namespace

    static_assert(!CanIntern<const char (&)[4]>);
    static_assert(!CanIntern<char (&)[16]>);
    static_assert(CanIntern<StringView>);
    static_assert(CanIntern<const char*>);
    static_assert(CanIntern<const String&>);

    static_assert(!CanFind<const char (&)[4]>);
    static_assert(!CanFind<char (&)[16]>);
    static_assert(CanFind<StringView>);
    static_assert(CanFind<const char*>);
    static_assert(CanFind<const String&>);

    TEST(PoolStringBanTest, ARuntimeBufferInternsThroughAStringView) {
        char buffer[] = "ban_buffer_text_7d1e";

        EXPECT_EQ(PoolString::Intern(StringView(buffer)), "ban_buffer_text_7d1e"_intern);
    }

    TEST(PoolStringBanTest, ARuntimeBufferIsFoundThroughAStringView) {
        char buffer[] = "ban_find_buffer_text_3a52";
        const PoolString interned = "ban_find_buffer_text_3a52"_intern;

        const Optional<PoolString> found = PoolString::Find(StringView(buffer));

        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, interned);
    }

}  // namespace BECore::Tests

#include <BECore/Reflection/AbstractFactory.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A factory base that no generated code registers into, so the singleton holds only what this file registers
        class RegisteredProduct : public RefCountedNonAtomic {
        public:
            int32_t creator = 0;
        };

        /// A second base, so the case that fills its factory does not change what another case counts
        class GrownProduct : public RefCountedNonAtomic {
        public:
            int32_t creator = 0;
        };

        template <int32_t TCreator, typename TProduct = RegisteredProduct>
        IntrusivePtr<TProduct> MakeProduct() {
            auto product = New<TProduct>();
            product->creator = TCreator;
            return product;
        }

        ClassMeta Meta(StringView name, StringView icon, AssetPurpose purpose) {
            return ClassMeta{StringUtils::GetHash(name), name, icon, purpose};
        }

        /// Enough types for the factory's index to rehash several times
        constexpr size_t kGrownTypes = 64;
        constexpr int32_t kReplacementCreator = -1;

        /// The factory keeps views of the names it is given, so they live as long as it does
        const Vector<String>& GrownNames() {
            static const Vector<String> names = [] {
                Vector<String> made;
                for (size_t i = 0; i < kGrownTypes; ++i) {
                    made.push_back("GrownProduct{}"_format(i));
                }
                return made;
            }();
            return names;
        }

        template <size_t... TIndices>
        auto GrownCreators(std::index_sequence<TIndices...>) {
            return Array<AbstractFactory<GrownProduct>::CreatorFunc, sizeof...(TIndices)>{&MakeProduct<static_cast<int32_t>(TIndices), GrownProduct>...};
        }

    }  // namespace

    TEST(AbstractFactoryTest, SecondRegistrationReplacesCreatorAndMeta) {
        auto& factory = AbstractFactory<RegisteredProduct>::GetInstance();
        const ClassMeta first = Meta("TwiceProduct", "box", AssetPurpose::Sim);
        const ClassMeta other = Meta("OtherProduct", "circle", AssetPurpose::Common);
        const ClassMeta again = Meta("TwiceProduct", "star", AssetPurpose::Visual);

        factory.Register(first, &MakeProduct<1>);
        factory.Register(other, &MakeProduct<2>);
        factory.Register(again, &MakeProduct<3>);

        const Vector<ClassMeta>& types = factory.GetRegisteredTypes();
        EXPECT_EQ(types.size(), 2u);
        EXPECT_EQ(At(types, 0).typeName, again.typeName);
        EXPECT_EQ(At(types, 0).icon, again.icon);
        EXPECT_EQ(At(types, 0).purpose, again.purpose);
        EXPECT_EQ(At(types, 1).typeName, other.typeName);

        const ClassMeta& found = Require(factory.FindMeta(again.typeName));
        EXPECT_EQ(found.icon, again.icon);
        EXPECT_EQ(found.purpose, again.purpose);

        EXPECT_EQ(Require(factory.Create(first)).creator, 3);
        EXPECT_EQ(Require(factory.Create(again.typeName)).creator, 3);
        EXPECT_EQ(Require(factory.Create("TwiceProduct"_intern)).creator, 3);
    }

    TEST(AbstractFactoryTest, EveryTypeKeepsItsOwnMetaAndCreatorAsTheFactoryGrows) {
        auto& factory = AbstractFactory<GrownProduct>::GetInstance();
        const Vector<String>& names = GrownNames();
        const auto creators = GrownCreators(std::make_index_sequence<kGrownTypes>{});
        for (size_t i = 0; i < kGrownTypes; ++i) {
            factory.Register(Meta(names[i], "box", AssetPurpose::Sim), creators[i]);
        }
        constexpr size_t replaced = kGrownTypes / 2;
        factory.Register(Meta(names[replaced], "star", AssetPurpose::Visual), &MakeProduct<kReplacementCreator, GrownProduct>);

        const Vector<ClassMeta>& types = factory.GetRegisteredTypes();
        ASSERT_EQ(types.size(), kGrownTypes);
        for (size_t i = 0; i < kGrownTypes; ++i) {
            const StringView name = names[i];
            SCOPED_TRACE(names[i].c_str());
            EXPECT_EQ(types[i].typeName, name);
            EXPECT_EQ(factory.FindMeta(name), &types[i]);
            EXPECT_EQ(types[i].icon, i == replaced ? StringView("star") : StringView("box"));
            EXPECT_EQ(Require(factory.Create(name)).creator, i == replaced ? kReplacementCreator : static_cast<int32_t>(i));
        }

        const StringView unknown = "NoSuchProduct";
        EXPECT_EQ(factory.FindMeta(unknown), nullptr);
        EXPECT_EQ(factory.Create(unknown).Get(), nullptr);
    }

}  // namespace BECore::Tests

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

        template <int32_t TCreator>
        IntrusivePtr<RegisteredProduct> MakeProduct() {
            auto product = New<RegisteredProduct>();
            product->creator = TCreator;
            return product;
        }

        ClassMeta Meta(StringView name, StringView icon, AssetPurpose purpose) {
            return ClassMeta{StringUtils::GetHash(name), name, icon, purpose};
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

}  // namespace BECore::Tests

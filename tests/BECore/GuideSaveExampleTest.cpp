#include <BECore/GuideSaveExample.h>
#include <BECore/Save/SaveGame.h>
#include <BECore/Save/SaveTestSupport.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestFiles.h>
#include <Generated/GuideSaveExample.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace GuideSaveExample;

        constexpr StringView kSlot = "slot1";
        constexpr int32_t kPlayedLevel = 4;
        constexpr int32_t kCoinsInTheWallet = 250;
        constexpr uint32_t kVersionOfTheNextGame = kLevelsFromOneVersion + 1;

        GuideProfileState MakeProfile() {
            GuideProfileState state;
            state._level = kPlayedLevel;
            state._name = "Mira";
            state._unlocked = {1, 2, 5};
            state._coins = kCoinsInTheWallet;
            return state;
        }

        void ExpectSameProfile(const GuideProfileState& actual, const GuideProfileState& expected) {
            EXPECT_EQ(actual._level, expected._level);
            EXPECT_EQ(actual._name, expected._name);
            EXPECT_EQ(actual._unlocked, expected._unlocked);
            EXPECT_EQ(actual._coins, expected._coins);
        }

        /// The kind of a failed result; a success is a failure of the case
        template <typename T>
        SaveErrorKind KindOf(const std::expected<T, SaveError>& result) {
            EXPECT_FALSE(result.has_value()) << "a failure was expected";
            return result.has_value() ? SaveErrorKind::NotInitialized : result.error().kind;
        }

    }  // namespace

    /// A service of its own on a temporary folder: no global service, so the cases run beside others
    class GuideSaveExampleTest : public SaveFolderTest {
    protected:
        void SetUp() override {
            ASSERT_NO_FATAL_FAILURE(SaveFolderTest::SetUp());
            _service.emplace(_fileSystem);
            ASSERT_TRUE(_service->Initialize(_saves));
        }

        void TearDown() override {
            _service.reset();
            SaveFolderTest::TearDown();
        }

        SaveGameService& Service() {
            return *_service;
        }

        Optional<SaveGameService> _service;
    };

    TEST_F(GuideSaveExampleTest, AProfileComesBackFromItsSlotAndTheFileIsWhole) {
        GuideProfile player(Service());
        player.State() = MakeProfile();
        ASSERT_TRUE(player.Save(kSlot));

        EXPECT_TRUE(IsFile(_saves / "slot1.save"));
        EXPECT_FALSE(PathExists(_saves / "slot1.save.tmp")) << "the temporary file is renamed over the save";

        GuideProfile restarted(Service());
        EXPECT_EQ(restarted.Load(kSlot), LoadOutcome::Loaded);
        ExpectSameProfile(restarted.State(), MakeProfile());
    }

    TEST_F(GuideSaveExampleTest, ASlotWithoutAFileIsANewGameAndIsNotLogged) {
        const LogRecorder log;
        GuideProfile player(Service());

        EXPECT_EQ(player.Load("nothing"), LoadOutcome::NewGame);
        LogEvent::Flush();

        ExpectSameProfile(player.State(), GuideProfileState{});
        EXPECT_EQ(log.Count(LogLevel::Error, {"nothing"}), 0u);
    }

    TEST_F(GuideSaveExampleTest, AFileOfVersion1IsReadAndFixedUpAndAFieldAddedLaterKeepsItsDefault) {
        GuideProfileStateV1 old;
        old._level = 3;  // counted from 0: the fourth level
        old._name = "Mira";
        old._unlocked = {1, 2};
        ASSERT_TRUE(Service().Save(kSlot, SaveSchema{kProfileSchema.game, 1, 1}, old).has_value());

        GuideProfile player(Service());
        EXPECT_EQ(player.Load(kSlot), LoadOutcome::Loaded);

        EXPECT_EQ(player.State()._level, kPlayedLevel) << "the fix-up counts from 1";
        EXPECT_EQ(player.State()._name, "Mira");
        EXPECT_EQ(player.State()._coins, GuideProfileState{}._coins) << "the file has no coins: the default of the new type";
    }

    TEST_F(GuideSaveExampleTest, AFileOfAnotherGameOrOfANewerVersionIsRefusedAndTheLiveStateIsUntouched) {
        ASSERT_TRUE(Service().Save("other", SaveSchema{"AnotherGame", 2, 1}, MakeProfile()).has_value());
        ASSERT_TRUE(Service().Save("newer", SaveSchema{kProfileSchema.game, kVersionOfTheNextGame, 1}, MakeProfile()).has_value());

        GuideProfileState probe;
        EXPECT_EQ(KindOf(Service().Load("other", kProfileSchema, probe)), SaveErrorKind::WrongGame);
        EXPECT_EQ(KindOf(Service().Load("newer", kProfileSchema, probe)), SaveErrorKind::VersionTooNew);

        GuideProfile player(Service());
        player.State()._level = 9;
        const LogRecorder log;
        EXPECT_EQ(player.Load("newer"), LoadOutcome::Refused);
        LogEvent::Flush();

        EXPECT_EQ(player.State()._level, 9);
        EXPECT_EQ(log.Count(LogLevel::Error, {"Cannot load 'newer'"}), 1u) << "one line, with the reason";
    }

    TEST_F(GuideSaveExampleTest, ADamagedFileIsRefusedWithAReasonLeftOnDiskAndTheLiveStateIsUntouched) {
        GuideProfile player(Service());
        player.State() = MakeProfile();
        ASSERT_TRUE(player.Save(kSlot));

        const Vector<uint8_t> whole = Require(Service().ReadSlot(kSlot));
        const Vector<uint8_t> cut(whole.begin(), whole.begin() + static_cast<ptrdiff_t>(whole.size() / 2));
        ASSERT_TRUE(Service().WriteSlot(kSlot, cut).has_value());

        GuideProfileState probe;
        const auto result = Service().Load(kSlot, kProfileSchema, probe);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().kind, SaveErrorKind::Corrupt);
        EXPECT_FALSE(result.error().detail.empty());

        player.State()._level = 9;
        EXPECT_EQ(player.Load(kSlot), LoadOutcome::Refused);
        EXPECT_EQ(player.State()._level, 9);
        EXPECT_EQ(Require(Service().ReadSlot(kSlot)), cut) << "a refused file is left as it was";
    }

    TEST_F(GuideSaveExampleTest, ABadSlotNameIsRefusedAndTheListShowsTheSlots) {
        GuideProfile player(Service());
        player.State() = MakeProfile();
        EXPECT_EQ(KindOf(Service().Save("../x", kProfileSchema, player.State())), SaveErrorKind::InvalidSlotName);
        EXPECT_FALSE(player.Save("../x"));

        ASSERT_TRUE(player.Save("beta"));
        ASSERT_TRUE(player.Save("alpha"));
        const Vector<PoolString> slots = Service().List();
        ASSERT_EQ(slots.size(), 2u);
        EXPECT_EQ(slots[0].ToStringView(), "alpha");
        EXPECT_EQ(slots[1].ToStringView(), "beta");
    }

}  // namespace BECore::Tests

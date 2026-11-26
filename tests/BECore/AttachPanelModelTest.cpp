// The Attach panel's model and the widget that owns the editor's link, over the fake program of FakeControlEndpoint.

#include <BECore/Application/CommandLine.h>
#include <BECore/FakeControlEndpoint.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/RemoteEditor/AttachPanelModel.h>
#include <Widgets/RemoteEditor/RemoteSessionRegistry.h>
#include <Widgets/RemoteEditor/RemoteSessionWidget.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kNodes = R"(<node name="Board"/>)";
        constexpr StringView kElsewhere = "ws://127.0.0.1:9000";

        bool Contains(const String& text, StringView part) {
            return StringView(text).find(part) != StringView::npos;
        }

        class AttachPanelModelTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _endpoint = MakeUnique<FakeControlEndpoint>(kNodes);
                _session = MakeUnique<RemoteSession>(_endpoint->Factory());
                _handle = RemoteSessionRegistry::GetInstance().Register(*_session);
                _model = New<AttachPanelModel>();
                _model->Update();
            }

            void TearDown() override {
                _model.Reset();
                _handle.Reset();
                _session.reset();
                _endpoint.reset();
            }

            [[nodiscard]] const AttachPanelView& View() const {
                return _model->GetView();
            }

            void Run(StringView command) {
                EXPECT_TRUE(_model->RunCommand(command, {}));
                _model->Update();
            }

            void Exchange() {
                _session->Poll(_now);
                _endpoint->Serve();
                _session->Poll(_now);
                _model->Update();
            }

            UniquePtr<FakeControlEndpoint> _endpoint;
            UniquePtr<RemoteSession> _session;
            RemoteSessionHandle _handle;
            IntrusivePtr<AttachPanelModel> _model;
            RemoteSessionTime _now{};
        };

    }  // namespace

    TEST_F(AttachPanelModelTest, AttachConnectsToTheTypedAddressAndDetachEndsTheLink) {
        EXPECT_EQ(View().address, kDefaultAttachAddress);
        EXPECT_EQ(View().status, "Not attached");
        EXPECT_TRUE(View().canAttach);
        EXPECT_FALSE(View().canDetach);

        _model->GetView().address = kElsewhere;
        Run(EditorCommandIds::kRemoteAttach.View());
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Connecting);
        EXPECT_EQ(_session->GetAddress(), kElsewhere);
        EXPECT_TRUE(Contains(View().status, "Connecting")) << View().status.c_str();
        EXPECT_TRUE(View().canDetach);

        Exchange();
        EXPECT_TRUE(Contains(View().status, "Attached")) << View().status.c_str();
        EXPECT_TRUE(Contains(View().status, kElsewhere)) << View().status.c_str();
        EXPECT_FALSE(View().troubled);

        Run(EditorCommandIds::kRemoteDetach.View());
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Detached);
        EXPECT_EQ(View().status, "Not attached");
        EXPECT_FALSE(View().canDetach);
    }

    TEST_F(AttachPanelModelTest, ARefusedAddressSaysWhyAndLeavesTheLinkAsItWas) {
        Run(EditorCommandIds::kRemoteAttach.View());
        Exchange();

        _model->GetView().address = "http://127.0.0.1";
        Run(EditorCommandIds::kRemoteAttach.View());
        EXPECT_TRUE(View().hasError);
        EXPECT_TRUE(Contains(View().error, "ws://")) << View().error.c_str();
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Attached);
        EXPECT_EQ(_session->GetAddress(), kDefaultAttachAddress);

        _model->GetView().address = kElsewhere;
        Run(EditorCommandIds::kRemoteAttach.View());
        EXPECT_FALSE(View().hasError);
        EXPECT_TRUE(View().error.empty());
    }

    TEST_F(AttachPanelModelTest, ALostLinkAndASilentProgramAreShownAsTroubles) {
        Run(EditorCommandIds::kRemoteAttach.View());
        Exchange();

        _session->Poll(_now += RemoteSessionTiming::kTreeInterval);
        _session->Poll(_now += RemoteSessionTiming::kStallAfter);
        _model->Update();
        EXPECT_TRUE(View().troubled);
        EXPECT_TRUE(Contains(View().status, "does not answer")) << View().status.c_str();

        _endpoint->Drop(kRoomTransportAbnormalClosure, "the game quit");
        Exchange();
        EXPECT_TRUE(View().troubled);
        EXPECT_TRUE(Contains(View().status, "Lost")) << View().status.c_str();
        EXPECT_TRUE(Contains(View().status, "the game quit")) << View().status.c_str();
        EXPECT_TRUE(View().canDetach);
    }

    TEST_F(AttachPanelModelTest, WithoutASessionThePanelSaysSoAndOffersNothing) {
        _handle.Reset();
        _model->Update();
        EXPECT_TRUE(Contains(View().status, "RemoteSessionWidget")) << View().status.c_str();
        EXPECT_FALSE(View().canAttach);
        EXPECT_FALSE(View().canDetach);
        Run(EditorCommandIds::kRemoteAttach.View());
        Run(EditorCommandIds::kRemoteDetach.View());

        _handle = RemoteSessionRegistry::GetInstance().Register(*_session);
        _model->Update();
        EXPECT_EQ(View().status, "Not attached");
        EXPECT_TRUE(View().canAttach);
    }

    TEST_F(AttachPanelModelTest, AnUpdateOfAnUnchangedLinkAllocatesNothing) {
        Run(EditorCommandIds::kRemoteAttach.View());
        Exchange();
        ExpectNoAllocations([this] { _model->Update(); });
    }

    /// The widget with the program's `--attach`, as a start of the editor makes it
    class RemoteSessionWidgetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            const Span<const String> arguments = CoreManager::GetCommandLine().GetArguments();
            _previousArguments.assign(arguments.begin(), arguments.end());
            const Array<String, 1> attach = {Format("{}={}", kAttachFlag, kElsewhere)};
            CoreManager::GetCommandLine().Set({attach.data(), attach.size()});
            RemoteSessionRegistry::DestroyInstance();
            _endpoint = MakeUnique<FakeControlEndpoint>(kNodes);
        }

        void TearDown() override {
            _endpoint.reset();
            RemoteSessionRegistry::DestroyInstance();
            CoreManager::GetCommandLine().Set({_previousArguments.data(), _previousArguments.size()});
        }

        [[nodiscard]] IntrusivePtr<RemoteSessionWidget> StartWidget() {
            auto widget = New<RemoteSessionWidget>(_endpoint->Factory());
            XmlDeserializer deserializer;
            EXPECT_TRUE(widget->Initialize(deserializer));
            return widget;
        }

        UniquePtr<FakeControlEndpoint> _endpoint;
        Vector<String> _previousArguments;
    };

    TEST_F(RemoteSessionWidgetTest, TheStartAttachesToTheFlagsAddressAndTheLinkEndsWithTheWidget) {
        IntrusivePtr<RemoteSessionWidget> widget = StartWidget();
        const RemoteSession& session = Require(RemoteSessionRegistry::GetInstance().Get());
        EXPECT_EQ(session.GetAddress(), kElsewhere);

        widget->Update();
        _endpoint->Serve();
        widget->Update();
        EXPECT_EQ(session.GetState(), RemoteSessionState::Attached);

        widget.Reset();
        EXPECT_EQ(RemoteSessionRegistry::GetInstance().Get(), nullptr);
        EXPECT_TRUE(_endpoint->Connection().destroyed);

        // The widget of a later switch back to the editor
        const IntrusivePtr<RemoteSessionWidget> next = StartWidget();
        EXPECT_EQ(Require(RemoteSessionRegistry::GetInstance().Get()).GetState(), RemoteSessionState::Detached);
        EXPECT_EQ(_endpoint->GetConnectionCount(), 1u);
    }

}  // namespace BECore::Tests

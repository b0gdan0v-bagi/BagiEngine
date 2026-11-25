#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/PanelAssets/IPanelModel.h>

namespace BECore::Tests {

    /// A model that counts its own lifetime and what the widget asks of it, in statics the test resets: the widget keeps its models private
    class CountingPanelModel : public IPanelModel {
        BE_CLASS(CountingPanelModel)
    public:
        CountingPanelModel() {
            ++created;
            ++alive;
        }

        ~CountingPanelModel() override {
            --alive;
        }

        void Update() override {
            ++updates;
        }

        bool RunCommand(StringView /*id*/, StringView /*argument*/) override {
            ++commands;
            return true;
        }

        inline static int created = 0;
        inline static int alive = 0;
        inline static int updates = 0;
        inline static int commands = 0;

        static void Reset() {
            created = 0;
            alive = 0;
            updates = 0;
            commands = 0;
        }

        BE_REFLECT_FIELD bool flag = false;
    };

    /// A model whose command runs a test-set hook (the test rewrites the panel's own file in it) and notes whether it was destroyed while the command ran
    class ReloadingPanelModel : public IPanelModel {
        BE_CLASS(ReloadingPanelModel)
    public:
        ReloadingPanelModel() {
            ++alive;
        }

        ~ReloadingPanelModel() override {
            --alive;
            if (_inCommand) {
                ++destroyedInCommand;
            }
        }

        bool RunCommand(StringView /*id*/, StringView /*argument*/) override {
            // The widget holds the only reference, and the hook may make it drop it
            _inCommand = true;
            if (hook) {
                hook();
            }
            _inCommand = false;
            ++commandsFinished;
            return true;
        }

        inline static MoveOnlyFunction<void()> hook;
        inline static int alive = 0;
        inline static int destroyedInCommand = 0;
        inline static int commandsFinished = 0;

        static void Reset() {
            hook = nullptr;
            alive = 0;
            destroyedInCommand = 0;
            commandsFinished = 0;
        }

    private:
        bool _inCommand = false;
    };

}  // namespace BECore::Tests

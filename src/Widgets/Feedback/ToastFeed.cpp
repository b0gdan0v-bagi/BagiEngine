#include "ToastFeed.h"

#include <BECore/Config/ConfigNames.h>
#include <BECore/Control/ControlActivity.h>

namespace BECore {

    namespace {

        constexpr StringView kSceneSavedText = "Scene saved";
        constexpr StringView kConnectedText = "Control API client connected";
        constexpr StringView kDisconnectedText = "Control API client disconnected";

    }  // namespace

    void ToastFeed::Connect(const ControlActivity& activity) {
        _activity = &activity;
        _seenVersion = activity.GetVersion();
        _nextSequence = activity.GetNextSequence();

        Subscribe<EditorToastEvent, &ToastFeed::OnToast>(this);
        Subscribe<ConfigEvents::ConfigChangedEvent, &ToastFeed::OnConfigChanged>(this);
        Subscribe<ConfigEvents::ConfigReloadFailedEvent, &ToastFeed::OnReloadFailed>(this);
    }

    void ToastFeed::Update(float seconds) {
        _list.Advance(seconds);
        ReadActivity();
    }

    void ToastFeed::ReadActivity() {
        if (_activity == nullptr || _activity->GetVersion() == _seenVersion) {
            return;
        }
        _seenVersion = _activity->GetVersion();

        size_t first = _activity->Size();
        while (first > 0 && _activity->At(first - 1).sequence >= _nextSequence) {
            --first;
        }
        for (size_t i = first; i < _activity->Size(); ++i) {
            const ControlActivityKind kind = _activity->At(i).kind;
            if (kind == ControlActivityKind::Connected) {
                _list.Push(ToastLevel::Info, kConnectedText);
            } else if (kind == ControlActivityKind::Disconnected) {
                _list.Push(ToastLevel::Info, kDisconnectedText);
            }
        }
        _nextSequence = _activity->GetNextSequence();
    }

    void ToastFeed::OnToast(const EditorToastEvent& event) {
        _list.Push(event.level, StringView(event.text.data(), event.text.size()));
    }

    void ToastFeed::OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event) {
        if (event.cause == ConfigEvents::ConfigChangeCause::Saved) {
            if (event.name == ConfigNames::scene) {
                _list.Push(ToastLevel::Success, kSceneSavedText);
            } else {
                _list.Push(ToastLevel::Success, "Saved {}"_format(event.name.ToStringView()));
            }
        } else if (event.cause == ConfigEvents::ConfigChangeCause::Reloaded) {
            _list.Push(ToastLevel::Info, "Config reloaded: {}"_format(event.name.ToStringView()));
        }
    }

    void ToastFeed::OnReloadFailed(const ConfigEvents::ConfigReloadFailedEvent& event) {
        _list.Push(ToastLevel::Error, "Config reload failed: {}"_format(event.name.ToStringView()));
    }

}  // namespace BECore

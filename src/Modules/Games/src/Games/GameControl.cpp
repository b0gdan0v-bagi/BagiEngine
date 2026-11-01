#include "GameControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <Games/GameManager.h>
#include <Games/IControllableGame.h>

namespace BECore {

    namespace {

        struct ActiveGame {
            IGame* game = nullptr;
            IControllableGame* control = nullptr;
        };

        /// The active game with its control interface, or the error to answer with
        std::expected<ActiveGame, ControlError> FindActiveControllable() {
            IGame* game = GameManager::GetInstance().GetActive();
            if (game == nullptr) {
                return std::unexpected(MakeControlError(GameControlErrors::kNoActiveGame, "no game is active"));
            }
            IControllableGame* control = game->AsControllable();
            if (control == nullptr) {
                return std::unexpected(MakeControlError(GameControlErrors::kNotControllable, Format("the game '{}' cannot be controlled from outside", game->GetName().ToStringView())));
            }
            return ActiveGame{game, control};
        }

        /// {"game": name, "version": state version}: what every answer about the active game starts with
        Json DescribeGame(const ActiveGame& active) {
            Json result = Json::object();
            result["game"] = MakeJsonString(active.game->GetName().ToStringView());
            result["version"] = active.control->GetStateVersion();
            return result;
        }

        /**
         * Publishes `state_changed` at the end of a frame when the state version of the active game moved since the
         * last look. It sees every change: an action of a client, a reset, a click of a person. The first look only
         * records the version, because there is nothing to compare with and no client can have missed anything.
         */
        class StateChangeWatcher {
        public:
            explicit StateChangeWatcher(ControlService& service) : _service(service) {}

            void OnFrameEnd() {
                const auto active = FindActiveControllable();
                if (!active) {
                    _seen.reset();
                    return;
                }

                const eastl::string name(active->game->GetName().ToStringView());
                const uint64_t version = active->control->GetStateVersion();
                if (_seen && _seen->game == name && _seen->version == version) {
                    return;
                }
                const bool firstLook = !_seen;
                _seen = Seen{name, version};
                if (firstLook) {
                    return;
                }

                Json data = DescribeGame(*active);
                if (auto state = active->control->GetState()) {
                    data["state"] = std::move(*state);
                }
                _service.Publish(GameControlEvents::kStateChanged, data);
            }

        private:
            struct Seen {
                eastl::string game;
                uint64_t version = 0;
            };

            ControlService& _service;
            std::optional<Seen> _seen;
        };

        void HandleListGames(ControlCall& call) {
            const GameManager& manager = GameManager::GetInstance();

            Json games = Json::array();
            for (const IntrusivePtr<IGame>& game : manager.GetAll()) {
                Json entry = Json::object();
                entry["name"] = MakeJsonString(game->GetName().ToStringView());
                entry["scene"] = MakeJsonString(game->GetSceneName().ToStringView());
                entry["controllable"] = game->AsControllable() != nullptr;
                games.push_back(std::move(entry));
            }

            const IGame* active = manager.GetActive();
            Json result = Json::object();
            result["active"] = active != nullptr ? MakeJsonString(active->GetName().ToStringView()) : Json();
            result["games"] = std::move(games);
            call.Reply(std::move(result));
        }

        void HandleSwitchGame(ControlCall& call) {
            const auto name = GetJsonString(call.Params(), "name");
            if (!name) {
                call.Fail(ControlErrors::kInvalidParams, "'name' has to be a string");
                return;
            }

            // Look the name up instead of interning it: a client must not grow the PoolString table
            GameManager& manager = GameManager::GetInstance();
            for (const IntrusivePtr<IGame>& game : manager.GetAll()) {
                if (game->GetName().ToStringView() == eastl::string_view(name->c_str(), name->size())) {
                    manager.SwitchTo(game->GetName());
                    Json result = Json::object();
                    result["active"] = MakeJsonString(game->GetName().ToStringView());
                    call.Reply(std::move(result));
                    return;
                }
            }
            call.Fail(GameControlErrors::kUnknownGame, Format("no game is named '{}'", *name));
        }

        void HandleReset(ControlCall& call) {
            IGame* game = GameManager::GetInstance().GetActive();
            if (game == nullptr) {
                call.Fail(GameControlErrors::kNoActiveGame, "no game is active");
                return;
            }

            IControllableGame* control = game->AsControllable();
            if (control == nullptr) {
                GameManager::GetInstance().ResetActive();
                Json result = Json::object();
                result["game"] = MakeJsonString(game->GetName().ToStringView());
                call.Reply(std::move(result));
                return;
            }

            if (auto reset = control->Reset(call.Params()); !reset) {
                call.Fail(reset.error());
                return;
            }
            call.Reply(DescribeGame(ActiveGame{game, control}));
        }

        void HandleGetState(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            auto state = active->control->GetState();
            if (!state) {
                call.Fail(state.error());
                return;
            }
            Json result = DescribeGame(*active);
            result["state"] = std::move(*state);
            call.Reply(std::move(result));
        }

        void HandleListActions(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            auto actions = active->control->GetActions();
            if (!actions) {
                call.Fail(actions.error());
                return;
            }
            Json result = DescribeGame(*active);
            result["actions"] = std::move(*actions);
            call.Reply(std::move(result));
        }

        void HandlePerformAction(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            auto outcome = active->control->PerformAction(call.Params());
            if (!outcome) {
                call.Fail(outcome.error());
                return;
            }
            Json result = DescribeGame(*active);  // the version after the action
            result["outcome"] = std::move(*outcome);
            call.Reply(std::move(result));
        }

        void HandleSetState(ControlCall& call) {
            const auto active = FindActiveControllable();
            if (!active) {
                call.Fail(active.error());
                return;
            }
            if (auto loaded = active->control->SetState(call.Params()); !loaded) {
                call.Fail(loaded.error());
                return;
            }
            call.Reply(DescribeGame(*active));
        }

    }  // namespace

    void RegisterGameControl(ControlService& service) {
        service.RegisterHandler("list_games", HandleListGames);
        service.RegisterHandler("switch_game", HandleSwitchGame);
        service.RegisterHandler("reset", HandleReset);
        service.RegisterHandler("get_state", HandleGetState);
        service.RegisterHandler("list_actions", HandleListActions);
        service.RegisterHandler("perform_action", HandlePerformAction);
        service.RegisterHandler("set_state", HandleSetState);

        auto watcher = eastl::make_shared<StateChangeWatcher>(service);
        service.SetFrameEndHook(GameControlEvents::kStateChanged, [watcher] { watcher->OnFrameEnd(); });
    }

}  // namespace BECore

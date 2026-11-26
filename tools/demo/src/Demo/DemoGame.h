#pragma once

#include <Games/IGame.h>

namespace BECore::Demo {

    /**
     * @brief The IGame of BagiDemo: no rules and no state, only the scene it shows. Everything the demo does is a component of that scene, so
     * what a person edits in the editor is what runs.
     */
    class DemoGame : public IGame {
        BE_CLASS(DemoGame)
    public:
        DemoGame() = default;
        ~DemoGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;
    };

}  // namespace BECore::Demo

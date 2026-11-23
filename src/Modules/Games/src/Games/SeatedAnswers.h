#pragma once

#include <BECore/Control/JsonWriter.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    // What an `ISeatedGame` answers, as the game's own `BE_CLASS` structs derived from these bases (`Games/CLAUDE.md`, "Seats"). The room holds a
    // `UniquePtr` to the base and writes it through the virtual generated `WriteJson`, so it needs no game type and builds no `Json`; the keys of
    // the base and of the derived struct merge in sorted order. The bases have no field: they are the type the registry lists the game's structs under.
    // Plain `Vector` / `String` fields, never the arena types: the room carries the answer from the game to the hub across calls.

    /// What one seat may see now. A derived view holds no hidden part of another seat, field by field.
    struct SeatedView {
        BE_CLASS(SeatedView)

        virtual ~SeatedView() = default;
    };

    /// One action a seat may send, in the form a client sends it back to `ISeatedGame::PerformSeatAction`
    struct SeatedAction {
        BE_CLASS(SeatedAction)

        virtual ~SeatedAction() = default;
    };

    /// What an applied action did; public information only
    struct SeatedOutcome {
        BE_CLASS(SeatedOutcome)

        virtual ~SeatedOutcome() = default;
    };

    /**
     * @brief The actions a seat may send now, written as a JSON **array**.
     *
     * Not reflected: a reflected struct writes an object, and the wire `actions` is an array. The elements are the game's `SeatedAction`s;
     * `SeatedActionList` holds them.
     */
    class SeatedActions {
    public:
        virtual ~SeatedActions() = default;

        [[nodiscard]] virtual size_t Count() const = 0;

        /// Writes one array, `[]` when there is no action
        virtual void WriteJson(JsonWriter& writer) const = 0;
    };

    /// A list of one game's action type; each element is written by its own generated `WriteJson`
    template <typename TAction>
    class SeatedActionList final : public SeatedActions {
        static_assert(std::is_base_of_v<SeatedAction, TAction>, "an element of the list is a SeatedAction of the game");

    public:
        void Add(TAction action) {
            _items.push_back(std::move(action));
        }

        [[nodiscard]] const Vector<TAction>& Items() const {
            return _items;
        }

        [[nodiscard]] size_t Count() const override {
            return _items.size();
        }

        void WriteJson(JsonWriter& writer) const override {
            writer.BeginArray();
            for (const TAction& action : _items) {
                action.WriteJson(writer);
            }
            writer.EndArray();
        }

    private:
        Vector<TAction> _items;
    };

}  // namespace BECore

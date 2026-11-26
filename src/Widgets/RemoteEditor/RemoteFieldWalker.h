#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Types/Types.h>
#include <Widgets/RemoteEditor/RemotePendingWrites.h>
#include <Widgets/RemoteEditor/RemoteSchema.h>

namespace BECore {

    /**
     * @brief Visits the values of a remote component as the generated `AcceptPropertyVisitor` visits a local one
     *
     * The types, their order and the attributes come from the schema, the values from `scene_get` with the pending writes on top. What differs from a
     * local visit, because the wire does not carry it or a write cannot do it: a list or a map allows no add or remove (what the visitor asks of one
     * is dropped), a map comes in the order of the text, whether an `Optional` holds a value and which alternative a `Variant` holds are read-only, a
     * `PoolString` is visited as a `String` (so the editor interns nothing), and a value the schema gives no shape for (`opaque`, `value`, `any`, an
     * unknown kind) is a read-only text unless it is an object of numbers a math type or a colour writes. A value that is absent or of another kind
     * than its type is left out.
     *
     * A walk allocates nothing once its texts have grown, as long as the visitor changes nothing.
     */
    class RemoteFieldWalker {
    public:
        /// Values nested deeper are left out: types of the schema may hold each other
        static constexpr size_t kMaxDepth = 64;

        /**
         * @brief Visits the fields of @p type with the values of @p fields
         * @param component The node, type name and index the writes are made for; its path is not used
         * @return true when an edit went into @p writes
         */
        bool Walk(const RemoteSchema& schema, const RemoteType& type, JsonView fields, const RemoteWriteTarget& component, RemotePendingWrites& writes, IPropertyVisitor& visitor);

    private:
        void WalkMembers(const RemoteType& type, JsonView object, size_t depth);
        /// A field between BeginField and EndField, as the generated visitor visits one
        void WalkField(const RemoteField& field, JsonView value, size_t depth);
        /// The value at `_path`, of the type @p type, under the label @p name; @p value is not valid when the text has none
        void WalkValue(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth);
        template <typename T>
        void WalkNumber(JsonView value, StringView name);
        /// A bool or a number of the kind @p kind; false for any other kind
        bool WalkScalar(TypeKind kind, JsonView value, StringView name);
        void WalkString(JsonView value, StringView name);
        void WalkEnum(const RemoteTypeDesc& type, JsonView value, StringView name);
        void WalkObject(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth);
        void WalkList(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth);
        void WalkMap(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth);
        void WalkPair(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth);
        void WalkOptional(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth);
        void WalkVariant(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth);
        /// An object of numbers under the member names of a math type or a colour, other than the member @p ignored; false when @p value is none
        bool WalkNumberGroup(JsonView value, StringView name, StringView ignored = {});
        void ShowText(JsonView value, StringView name);

        /// Runs @p visit with the current field marked `readonly`: for what the program takes no write of
        template <typename Fn>
        void ReadOnly(Fn&& visit);

        /// Appends @p member to `_path` as a member name
        void AppendMember(StringView member);
        [[nodiscard]] RemoteWriteTarget Target() const;
        [[nodiscard]] const PropertyValue* FindOverride() const;
        void Emit(PropertyValue value, RemoteEditKind kind = RemoteEditKind::Value);

        // Of the walk in progress
        const RemoteSchema* _schema = nullptr;
        RemotePendingWrites* _writes = nullptr;
        IPropertyVisitor* _visitor = nullptr;
        RemoteWriteTarget _component{};
        const FieldAttributes* _attributes = nullptr;
        /// A `readonly` or `hidden` field is open: the program refuses a write below it
        bool _locked = false;
        bool _changed = false;

        // Kept from walk to walk for their capacity
        String _path;
        String _text;
        /// The map keys that need decoding, one per depth: a key labels its value while the value's own maps are walked
        Array<String, kMaxDepth> _keys;
    };

}  // namespace BECore

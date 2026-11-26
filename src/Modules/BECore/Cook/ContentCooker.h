#pragma once

#include <BECore/RefCounted/New.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/ValueLinkScope.h>

namespace BECore {

    struct ContentCooker;

    /**
     * @brief Reads one config document from @p reader (positioned at its root, configured by the cook: diagnostics Error, strict values as
     *        the cooker asks) and writes it to @p writer as a cooked content file
     *
     * Every error and every warning of the read is appended to @p findings (`<path>: <message>`, AppendFindings). **When there is
     * any finding nothing may be written**: the cook then reports the file as failed and no `.bin` exists for it. A cooked file starts
     * with `WriteContentStamp(writer, cooker.rootType)` and the root's own fields follow in the root scope.
     */
    using CookFunction = void(const ContentCooker& cooker, IDeserializer& reader, ISerializer& writer, Vector<String>& findings);

    /**
     * @brief Reads the same document as the cook function and returns the text of the object read (an `XmlSerializer` document): the unit
     *        by which a cooked file is compared with its source. @p reader is an XML or a binary reader, any diagnostics mode
     */
    using DescribeFunction = String(const ContentCooker& cooker, IDeserializer& reader);

    /// A new object of a cooker's root type, in the state the cook reads a file into
    using MakeRootFunction = OwnedObject();

    /**
     * @brief How one config file is cooked: the file of the stem @p stem is read as the reflected type @p rootType
     *
     * The strings are views of literals (a registrar's arguments): they outlive the registry.
     */
    struct ContentCooker {
        StringView stem;      ///< The file stem the cooker claims (a config is found by stem in every folder of the layer)
        StringView rootType;  ///< The type the file is stamped with: its layout hash is what a loader checks the file against
        CookFunction* cook = nullptr;
        DescribeFunction* describe = nullptr;
        bool strictValues = false;             ///< The cook reads the XML with `SetStrictValues(true)`: a malformed number or boolean fails the cook, as the loader's own strict read would
        MakeRootFunction* makeRoot = nullptr;  ///< Null for a hand-written cooker: it names no reflected object that the file reads as
    };

    /**
     * @brief The cookers of the program, found by file stem
     *
     * Modules fill it from namespace-scope registrars (`CookableConfig`), so a library that holds one must be linked whole (as for the
     * factories and `TypeRegistry`). Registration is static-initializer work: a read after `main` starts takes no lock. The
     * constructor is public so that a test can use a registry of its own; the engine uses `GetInstance()`.
     */
    class ContentCookRegistry {
    public:
        ContentCookRegistry() = default;
        ContentCookRegistry(const ContentCookRegistry&) = delete;
        ContentCookRegistry& operator=(const ContentCookRegistry&) = delete;

        static ContentCookRegistry& GetInstance();

        /// Adds @p cooker; a second one for a stem is a programming error (`ASSERT`), and the first one stays
        void Register(const ContentCooker& cooker);

        /// The cooker of the file stem @p stem, or null; allocates nothing
        [[nodiscard]] const ContentCooker* Find(StringView stem) const;

        /// Every cooker, in registration order (not stable across builds)
        [[nodiscard]] const Vector<ContentCooker>& All() const {
            return _cookers;
        }

    private:
        Vector<ContentCooker> _cookers;
    };

    /// Appends @p messages to @p findings as `<path>: <message>` (the message alone when the path is empty)
    void AppendFindings(const Vector<DeserializeError>& messages, Vector<String>& findings);

    /// Appends the errors, then the warnings, of @p reader to @p findings
    void AppendFindings(const IDeserializer& reader, Vector<String>& findings);

    namespace CookDetail {

        /// A config object: reference counted types are made as `New` makes them, a plain struct (`ApplicationConfig`) on the heap
        template <typename T>
        auto MakeConfigObject() {
            if constexpr (std::is_base_of_v<RefCountedAtomic, T> || std::is_base_of_v<RefCounted, T>) {
                return New<T>();
            } else {
                return MakeUnique<T>();
            }
        }

        template <typename T>
        OwnedObject MakeRootObject() {
            return OwnedObject::Adopt(MakeConfigObject<T>());
        }

        template <typename T>
        void CookReflected(const ContentCooker& cooker, IDeserializer& reader, ISerializer& writer, Vector<String>& findings) {
            const auto object = MakeConfigObject<T>();
            {
                const ValueLinkScope scope(cooker.stem);
                object->Deserialize(reader);
                reader.FinishLoad();
            }
            AppendFindings(reader, findings);
            if (!findings.empty()) {
                return;
            }
            if (!WriteContentStamp(writer, cooker.rootType)) {
                findings.push_back(Format("the type '{}' is not reflected in this program", cooker.rootType));
                return;
            }
            object->Serialize(writer);
        }

        template <typename T>
        String DescribeReflected(const ContentCooker& cooker, IDeserializer& reader) {
            const auto object = MakeConfigObject<T>();
            {
                const ValueLinkScope scope(cooker.stem);
                object->Deserialize(reader);
                reader.FinishLoad();
            }
            XmlSerializer text;
            object->Serialize(text);
            return text.SaveToString();
        }

    }  // namespace CookDetail

    /**
     * @brief A registrar object (namespace scope, in the module that owns the config) that makes the config of stem @p stem cookable
     *        as the reflected type @p T
     *
     * The type is read as `MakeFromConfig<T>` reads it (a new object, `RefCounted` or a plain struct, a `ValueLinkScope` named after the stem so `<context>` and `from` of a
     * `Value` field work, `FinishLoad`) and written with its own `Serialize`. A registration claims that the file reads the same
     * from its XML and from its `.bin`: a type that writes state its reader does not take, or whose `Serialize` is not the inverse of
     * its read, must not be registered (the equivalence check, `VerifyCooked`, is what finds out).
     *
     * @tparam T A reflected type, `RefCounted` or a plain struct; its `GetStaticTypeName()` is the stamped root type
     */
    template <typename T>
    class CookableConfig {
    public:
        /// @p stem is a literal; @p strictValues as `ContentCooker::strictValues`
        explicit CookableConfig(StringView stem, bool strictValues = false, ContentCookRegistry& registry = ContentCookRegistry::GetInstance()) {
            registry.Register({stem, T::GetStaticTypeName(), &CookDetail::CookReflected<T>, &CookDetail::DescribeReflected<T>, strictValues, &CookDetail::MakeRootObject<T>});
        }
    };

    /// A registrar for a config that a hand-written reader and writer cook (`TextureLibrary`): @p cooker as it is
    class CookableConfigWith {
    public:
        explicit CookableConfigWith(const ContentCooker& cooker, ContentCookRegistry& registry = ContentCookRegistry::GetInstance()) {
            registry.Register(cooker);
        }
    };

}  // namespace BECore

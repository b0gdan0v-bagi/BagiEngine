#pragma once

#include <BECore/Reflection/IPropertyVisitor.h>

namespace BECore::Tests {

    /// A property visitor that edits nothing and overrides no hook: a test visitor derives from it and overrides only what it looks at, so a new
    /// Visit overload of IPropertyVisitor is added here once, not in every test
    class NullPropertyVisitor : public IPropertyVisitor {
    public:
        bool Visit(StringView name, bool&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, int8_t&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, uint8_t&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, int16_t&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, uint16_t&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, int32_t&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, uint32_t&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, int64_t&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, uint64_t&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, float&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, double&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, PoolString&) override {
            return VisitValue(name);
        }
        bool Visit(StringView name, String&) override {
            return VisitValue(name);
        }

    protected:
        /// Every Visit of a value comes here unless the test overrides that overload; true would mean the value was edited
        virtual bool VisitValue(StringView /*name*/) {
            return false;
        }
    };

}  // namespace BECore::Tests

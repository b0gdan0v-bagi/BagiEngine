#include "FuzzAssertAbort.h"

#include <BECore/Assert/AssertEvent.h>
#include <cstdio>
#include <cstdlib>

namespace BECore::Fuzz {

    namespace {

        class AssertAbort : public SubscriptionHolder {
        public:
            void Install() {
                Subscribe<AssertEvent, &AssertAbort::OnAssert>(this);
            }

        private:
            void OnAssert(const AssertEvent& event) {
                std::fprintf(stderr, "Engine assertion reached by the input: %s%s%s (%s:%d)\n", event.expression != nullptr ? event.expression : "FATALERROR", event.message != nullptr ? " - " : "",
                             event.message != nullptr ? event.message : "", event.file, event.line);
                std::fflush(stderr);
                std::abort();
            }
        };

    }  // namespace

    void InstallAssertAbort() {
        // Never destroyed on purpose: at exit the event dispatcher (a static of its own) can be gone before a static handler, and the
        // handler's unsubscribe would then touch freed memory
        [[maybe_unused]] static AssertAbort* const handler = [] {
            auto* created = new AssertAbort();
            created->Install();
            return created;
        }();
    }

}  // namespace BECore::Fuzz

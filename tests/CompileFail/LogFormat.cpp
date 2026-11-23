// What a wrong LOG_X format must do: not compile. CheckCompileFail.cmake builds each case, LOG_FORMAT_CASE picks it.
// An extra argument is no case: {fmt} ignores it.
// Case 0 is the control: it must compile, so a failure of the others is the format check and not a missing include.
#include <BECore/Logger/Logger.h>

namespace BECore::CompileFail {

    struct NoFormatter {};

    void LogWithFormats() {
#if LOG_FORMAT_CASE == 0
        LOG_INFO("{} and {}", 1, "two");
#elif LOG_FORMAT_CASE == 1
        LOG_INFO("{} and {}", 1);  // too few arguments
#elif LOG_FORMAT_CASE == 2
        LOG_INFO("{}", NoFormatter{});  // a type with no formatter
#elif LOG_FORMAT_CASE == 3
        LOG_INFO("{:d}", "text");  // a spec that does not fit the type
#endif
    }

}  // namespace BECore::CompileFail

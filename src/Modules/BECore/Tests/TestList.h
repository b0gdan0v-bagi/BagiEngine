#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Tests/ITest.h>
#include <BECore/Types/Types.h>

namespace BECore::Tests {

    /**
     * @brief The enabled runtime tests of TestsConfig, in file order
     *
     * Filled by MakeFromConfig<TestList>("TestsConfig"): the loader skips `enabled="false"` entries and drops an unknown
     * `type` with a reported error.
     */
    struct TestList : RefCounted {
        BE_CLASS(TestList)

        BE_REFLECT_FIELD Vector<IntrusivePtrAtomic<ITest>> tests;
    };

}  // namespace BECore::Tests

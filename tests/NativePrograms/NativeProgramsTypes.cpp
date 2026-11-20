// The reflection of the test types the set's formulas read. The .gen.hpp defines functions outside a class template, so one executable holds one copy:
// BagiEngineTests gets its copy from the tests that use the types, and this tool, which has no tests, from here (the toy `DomainBoard` the registered operations name
// is one of them).

#include <Generated/ProgramDomainTypes.gen.hpp>
#include <Generated/ProgramRowTypes.gen.hpp>
#include <Generated/ValueTestTypes.gen.hpp>

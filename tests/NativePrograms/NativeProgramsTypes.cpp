// The reflection of the test types the set's formulas read. The .gen.hpp defines functions outside a class template, so one executable holds one copy:
// BagiEngineTests gets its copy from the tests that use the types, and this tool, which has no tests, from here.

#include <Generated/ProgramListTypes.gen.hpp>
#include <Generated/ProgramRowTypes.gen.hpp>
#include <Generated/ValueTestTypes.gen.hpp>

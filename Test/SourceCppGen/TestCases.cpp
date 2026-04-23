#include "HelloWorld.h"

using namespace vl;
using namespace vl::console;
using namespace vl::reflection::description;

void LoadTestCaseTypes()
{
}

TEST_FILE
{

TEST_CASE(L"HelloWorld")
{
	WString expected = L"Hello, world!";
	WString actual = ::vl_workflow_global::HelloWorld::Instance().main();
	Console::WriteLine(L"    expected : " + expected);
	Console::WriteLine(L"    actual   : " + actual);
	TEST_ASSERT(actual == expected);
});
}

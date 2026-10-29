#include "cunit.h"


static void test_simple_case(void)
{
    YUNIT_ASSERT(1);
}

static int init(void)
{
    return 0;
}

static int cleanup(void)
{
    return 0;
}

static void setup(void)
{

}

static void teardown(void)
{

}

static cunit_test_case_t aos_cjson_testcases[] = {
    { "simple", test_simple_case },
    YUNIT_TEST_CASE_NULL
};

static cunit_test_suite_t suites[] = {
    { "test", init, cleanup, setup, teardown, aos_cjson_testcases },
    YUNIT_TEST_SUITE_NULL
};

void test_cjson(void)
{
    cunit_add_test_suites(suites);
}
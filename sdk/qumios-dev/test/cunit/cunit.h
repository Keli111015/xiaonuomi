#ifndef CUNIT_TEST_H
#define CUNIT_TEST_H

#include "qm_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef void (*cunit_test_case_proc)(void);

typedef struct {
    const char          *name;
    cunit_test_case_proc test_proc;
} cunit_test_case_t;

#define CUNIT_TEST_CASE_NULL { 0 }

typedef int (*cunit_test_suit_init)(void);
typedef int (*cunit_test_suit_deinit)(void);
typedef void (*cunit_test_case_setup)(void);
typedef void (*cunit_test_case_teardown)(void);

typedef struct {
    const char              *name;
    cunit_test_suit_init     init;
    cunit_test_suit_deinit   deinit;
    cunit_test_case_setup    setup;
    cunit_test_case_teardown teardown;
    cunit_test_case_t       *test_case_array;
} cunit_test_suite_t;

#define CUNIT_TEST_SUITE_NULL { 0 }

void *cunit_add_test_suite(
         const char *name,
         cunit_test_suit_init init,
         cunit_test_suit_deinit deinit,
         cunit_test_case_setup setup,
         cunit_test_case_teardown teardown);

int cunit_add_test_case(
         void *test_suite,
         const char *name, cunit_test_case_proc proc);
int cunit_add_test_suites(cunit_test_suite_t *test_suite_array);

void *cunit_get_test_suite(const char *name);
void *cunit_get_test_case(void *test_suite, const char *name);

void cunit_test_init(void);
void cunit_test_deinit(void);
int cunit_test_run(void);
int cunit_run_test_suite(void *test_suite, int test_times);
int cunit_run_test_case(void *test_suite, void *test_case);
void cunit_test_print_result(void);

enum {
   TEST_RESULT_SUCCESS = 0,
   TEST_RESULT_FAILURE,
   TEST_RESULT_FATAL
};
void cunit_add_test_case_result(
         int result_type,
         const char *file, size_t line, const char *fmt, ...);

#define CUNIT_FAIL(msg) \
    cunit_add_test_case_result( \
        TEST_RESULT_FAILURE, __FILE__, __LINE__, "%s", msg);

#define CUNIT_FAIL_FATAL(msg) \
    cunit_add_test_case_result( \
        TEST_RESULT_FATAL, __FILE__, __LINE__, "%s", msg);

#define CUNIT_ASSERT(expr) \
    do { \
       cunit_add_test_case_result( \
           (expr) ? TEST_RESULT_SUCCESS : TEST_RESULT_FAILURE, \
            __FILE__, __LINE__, "%s", #expr); \
    } while (0);

/* print msg if failure, else nothing */
#define CUNIT_ASSERT_MSG(expr, fmt, args...) \
    do { \
       cunit_add_test_case_result( \
           (expr) ? TEST_RESULT_SUCCESS : TEST_RESULT_FAILURE, \
            __FILE__, __LINE__, "expect (%s) but actual ("fmt")", #expr, ##args); \
    } while (0);

#define CUNIT_ASSERT_FETAL(expr) \
    do { \
       cunit_add_test_case_result( \
           (expr) ? TEST_RESULT_SUCCESS : TEST_RESULT_FATAL, \
            __FILE__, __LINE__, "%s", #expr); \
    } while (0);

#define CUNIT_ASSERT_TRUE(expr) CUNIT_ASSERT(expr)

#define CUNIT_ASSERT_FALSE(expr) CUNIT_ASSERT(!(expr))

#define CUNIT_ASSERT_TRUE_FATAL(expr) CUNIT_ASSERT_FETAL(expr)

#define CUNIT_ASSERT_EQUAL(expect, actual) \
    do { \
       int result = (expect == actual); \
       cunit_add_test_case_result( \
           result ? TEST_RESULT_SUCCESS : TEST_RESULT_FAILURE, \
            __FILE__, __LINE__, \
           "%s==%s", #expect, #actual); \
    } while (0);

#define CUNIT_ASSERT_EQUAL_FATAL(expect, actual) \
    do { \
       cunit_add_test_case_result( \
           (expect == actual) ? TEST_RESULT_SUCCESS : TEST_RESULT_FATAL, \
            __FILE__, __LINE__, "%s==%s", #expect, #actual); \
    } while (0);

#define CUNIT_ASSERT_STR_EQUAL(expect, actual) \
    do { \
       int result = strcmp((const char *)expect, (const char *)actual) == 0; \
       cunit_add_test_case_result( \
           result ? TEST_RESULT_SUCCESS : TEST_RESULT_FAILURE, \
            __FILE__, __LINE__, \
            result ? "%s(%s)==%s(%s)" : "expect %s(%s) but actual is %s(%s)", \
           #expect, (const char *)expect, #actual, (const char *)actual); \
    } while (0);

#define CUNIT_ASSERT_STR_N_EQUAL(expect, actual, len) \
    do { \
       int result = strncmp((const char *)expect, (const char *)actual, len) == 0; \
       cunit_add_test_case_result( \
           result ? TEST_RESULT_SUCCESS : TEST_RESULT_FAILURE, \
           __FILE__, __LINE__, \
           result ? "%s(%s)==%s(%s) len=%d" \
                  : "expect %s(%s) but actual is %s(%s) len=%d", \
           #expect, (const char *)expect, #actual, (const char *)actual, len); \
    } while (0);

#define CUNIT_ASSERT_PTR_NULL(p) CUNIT_ASSERT_PTR_EQUAL(p, NULL)

#define CUNIT_ASSERT_PTR_NOT_NULL(p) CUNIT_ASSERT_PTR_NOT_EQUAL(p, NULL)

#define CUNIT_ASSERT_PTR_EQUAL(expect, actual) \
    do { \
       int result = (expect == actual); \
       cunit_add_test_case_result( \
           result ? TEST_RESULT_SUCCESS : TEST_RESULT_FAILURE, \
            __FILE__, __LINE__, \
           result ? "%s(%p)==%s(%p)" : "expect %s(%p) but actual is %s(%p)", \
           #expect, expect, #actual, actual); \
    } while (0);

#define CUNIT_ASSERT_PTR_NOT_EQUAL(expect, actual) \
    do { \
       int result = (expect != actual); \
       cunit_add_test_case_result( \
           result ? TEST_RESULT_SUCCESS : TEST_RESULT_FAILURE, \
            __FILE__, __LINE__, \
           result ? "%s(%p)!=%s(%p)" : "expect %s(%p) but actual is %s(%p)", \
           #expect, expect, #actual, actual); \
    } while (0);


#ifdef __cplusplus
}
#endif

#endif /* CUNIT_TEST_H */


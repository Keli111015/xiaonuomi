#include "cunit.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "qm_errno.h"

#define LOG_TAG "cunit"

typedef struct cunit_test_case_node_s
{
    struct cunit_test_case_node_s *next;
    const char *name;
    cunit_test_case_proc test_proc;
} cunit_test_case_node_t;

typedef struct cunit_test_suite_node_s
{
    struct cunit_test_suite_node_s *next;
    const char *name;
    cunit_test_suit_init init;
    cunit_test_suit_deinit deinit;
    cunit_test_case_setup setup;
    cunit_test_case_teardown teardown;
    cunit_test_case_node_t test_case_list_header;
} cunit_test_suite_node_t;

#define TEST_RESULT_MESSAGE_LENGTH 64
typedef struct cunit_test_case_result_s
{
    struct cunit_test_case_result_s *next;
    const char *file;
    size_t line;
    char msg[TEST_RESULT_MESSAGE_LENGTH];
} cunit_test_case_result_t;

typedef struct
{
    cunit_test_suite_node_t test_suite_list_header;
    int all_test_cases_count;
    int failed_test_cases_count;
    int fatal_test_cases_count;
    cunit_test_case_result_t failed_test_case_result;
} cunit_test_context_t;

static int cunit_run_test_case_list(
    cunit_test_suite_node_t *suite,
    cunit_test_case_node_t *test_case_list_header);

cunit_test_context_t g_test_context;

void cunit_test_init(void)
{
    memset(&g_test_context, 0, sizeof(cunit_test_context_t));
}

void cunit_test_deinit(void)
{
    cunit_test_suite_node_t *test_suite_node = g_test_context.test_suite_list_header.next;
    while (test_suite_node != NULL)
    {
        cunit_test_case_node_t *test_case_node = test_suite_node->test_case_list_header.next;
        while (test_case_node != NULL)
        {
            cunit_test_case_node_t *test_case_node_next = test_case_node->next;
            qm_free(test_case_node);
            test_case_node = test_case_node_next;
        }

        cunit_test_suite_node_t *test_suite_node_next = test_suite_node->next;
        qm_free(test_suite_node);
        test_suite_node = test_suite_node_next;
    }

    cunit_test_case_result_t *test_case_result = g_test_context.failed_test_case_result.next;
    while (test_case_result != NULL)
    {
        cunit_test_case_result_t *test_case_result_next = test_case_result->next;
        qm_free(test_case_result);
        test_case_result = test_case_result_next;
    }
}

void *cunit_add_test_suite(
    const char *name,
    cunit_test_suit_init init,
    cunit_test_suit_deinit deinit,
    cunit_test_case_setup setup,
    cunit_test_case_teardown teardown)
{
    cunit_test_suite_node_t *node = qm_malloc(sizeof(cunit_test_suite_node_t));
    if (node == NULL)
    {
        QM_LOGD(LOG_TAG, "%s", "out of memory");
        return NULL;
    }
    memset(node, 0, sizeof(cunit_test_suite_node_t));

    node->next = NULL;
    node->name = name;
    node->init = init;
    node->deinit = deinit;
    node->setup = setup;
    node->teardown = teardown;

    cunit_test_suite_node_t *prev = &g_test_context.test_suite_list_header;
    while (prev->next != NULL)
    {
        prev = prev->next;
    }

    prev->next = node;

    return node;
}

int cunit_add_test_case(void *test_suite, const char *name, cunit_test_case_proc proc)
{
    cunit_test_suite_node_t *test_suite_node = test_suite;

    cunit_test_case_node_t *prev = &test_suite_node->test_case_list_header;
    while (prev->next != NULL)
    {
        prev = prev->next;
    }

    cunit_test_case_node_t *node = qm_malloc(sizeof(cunit_test_case_node_t));
    memset(node, 0, sizeof(cunit_test_case_node_t));

    if (node == NULL)
    {
        QM_LOGD(LOG_TAG, "%s", "out of memory");
        return -QM_EINVAL;
    }
    node->name = name;
    node->test_proc = proc;

    prev->next = node;

    return 0;
}

int cunit_add_test_suites(cunit_test_suite_t *test_suit_array)
{
    int i = 0;
    while (test_suit_array[i].name != NULL)
    {
        cunit_test_suite_t *test_suite = &test_suit_array[i++];
        void *test_suite_node = cunit_add_test_suite(
            test_suite->name,
            test_suite->init, test_suite->deinit,
            test_suite->setup, test_suite->teardown);

        if (test_suite_node == NULL)
        {
            return -1;
        }

        int j = 0;
        cunit_test_case_t *test_case_array = test_suite->test_case_array;
        while (test_case_array[j].name != NULL)
        {
            cunit_test_case_t *test_case = &test_case_array[j++];
            int ret = cunit_add_test_case(
                test_suite_node,
                test_case->name, test_case->test_proc);
            if (ret == -1)
            {
                return ret;
            }
        }
    }

    return 0;
}

void *cunit_get_test_suite(const char *name)
{
    cunit_test_suite_node_t *node = g_test_context.test_suite_list_header.next;
    while (node != NULL)
    {
        if (strcmp(node->name, name) == 0)
        {
            return node;
        }
        node = node->next;
    }

    return NULL;
}

void *cunit_get_test_case(void *test_suite, const char *name)
{
    cunit_test_suite_node_t *test_suite_node = test_suite;

    cunit_test_case_node_t *node = test_suite_node->test_case_list_header.next;
    while (node != NULL)
    {
        if (strcmp(node->name, name) == 0)
        {
            return node;
        }
        node = node->next;
    }

    return NULL;
}

int cunit_test_run(void)
{
    int ret = 0;

    cunit_test_suite_node_t *test_suite = g_test_context.test_suite_list_header.next;
    while (test_suite != NULL)
    {
        ret = cunit_run_test_suite(test_suite, 1);
        if (ret != 0)
        {
            return ret;
        }
        test_suite = test_suite->next;
    }

    return ret;
}

int cunit_run_test_suite(void *test_suite, int test_times)
{
    int ret = 0;
    int index = 0;

    cunit_test_suite_node_t *node = NULL;

    if (NULL == test_suite || test_times < 1)
    {
        QM_LOGD(LOG_TAG, "param error\n");
        return -1;
    }

    node = test_suite;

    QM_LOGD(LOG_TAG, "Run test suite:%s", node->name);

    for (index = 0; index < test_times; index++)
    {
        if (node->init != NULL)
        {
            node->init();
        }

        ret = cunit_run_test_case_list(node, &node->test_case_list_header);

        if (node->deinit != NULL)
        {
            node->deinit();
        }

        if (ret != 0)
        {
            QM_LOGD(LOG_TAG, "Run test suite %s index:%d failed ret:%d", node->name, index + 1, ret);
        }
    }

    return 0;
}

static int _run_test_case(void *test_suite, void *test_case)
{
    cunit_test_suite_node_t *test_suite_node = test_suite;
    cunit_test_case_node_t *test_case_node = test_case;
    int cnt1 = g_test_context.failed_test_cases_count + g_test_context.fatal_test_cases_count;
    int cnt2;
    uint32_t now = qm_now_ms();
    uint32_t delta;

    if (test_suite_node->setup != NULL)
    {
        test_suite_node->setup();
    }

    if (test_case_node->test_proc != NULL)
    {
        test_case_node->test_proc();
    }

    if (test_suite_node->teardown != NULL)
    {
        test_suite_node->teardown();
    }

    cnt2 = g_test_context.failed_test_cases_count + g_test_context.fatal_test_cases_count;
    delta = qm_now_ms() - now;

    if ((cnt2 - cnt1) > 0) //There are some failed
    {
        QM_LOGD(LOG_TAG, "Run test case:%s finished elapsed time:%d ms failed:%d", test_case_node->name, delta, cnt2 - cnt1);
    }
    else
    {
        QM_LOGD(LOG_TAG, "Run test case:%s finished elapsed time:%d ms", test_case_node->name, delta);
    }

    return 0;
}

int cunit_run_test_case(void *test_suite, void *test_case)
{
    int ret = 0;

    cunit_test_suite_node_t *tnode = test_suite;
    cunit_test_case_node_t *tcase = test_case;

    QM_LOGD(LOG_TAG, "Run test suites %s", tnode->name);

    if (tnode->init != NULL)
    {
        tnode->init();
    }

    ret = _run_test_case(tnode, tcase);

    if (tnode->deinit != NULL)
    {
        tnode->deinit();
    }

    if (ret != 0)
    {
        QM_LOGD(LOG_TAG, "Suite:%s fail ret = %d", tnode->name, ret);
        return ret;
    }

    return 0;
}

int cunit_run_test_case_list(cunit_test_suite_node_t *test_suite,
                             cunit_test_case_node_t *test_case_list_header)
{
    int ret = 0;

    cunit_test_case_node_t *node = test_case_list_header->next;

    while (node != NULL)
    {
        ret = _run_test_case(test_suite, node);
        if (ret != 0)
        {
            return ret;
        }

        node = node->next;
    }

    return ret;
}

void cunit_add_test_case_result(int type, const char *file, size_t line, const char *fmt, ...)
{
    g_test_context.all_test_cases_count++;

    if (type == TEST_RESULT_SUCCESS)
    {
        return;
    }

    if (type == TEST_RESULT_FAILURE)
    {
        g_test_context.failed_test_cases_count++;
    }
    else if (type == TEST_RESULT_FATAL)
    {
        g_test_context.fatal_test_cases_count++;
    }
    else
    {
    }

    cunit_test_case_result_t *result = qm_malloc(sizeof(cunit_test_case_result_t));
    if (result == NULL)
    {
        QM_LOGD(LOG_TAG, "%s", "out of memory");
        return;
    }
    memset(result, 0, sizeof(cunit_test_case_result_t));

    va_list args;
    va_start(args, fmt);
    vsnprintf(result->msg, TEST_RESULT_MESSAGE_LENGTH, fmt, args);
    va_end(args);

    result->file = file;
    result->line = line;

    cunit_test_case_result_t *prev = &g_test_context.failed_test_case_result;
    while (prev != NULL && prev->next != NULL)
    {
        prev = prev->next;
    }

    prev->next = result;
}

void cunit_test_print_result(void)
{
    int failed_times = 0;
    int passed_times = 0;

    QM_LOGD(LOG_TAG, "\n--------------------------------\n");

    failed_times = g_test_context.failed_test_cases_count + g_test_context.fatal_test_cases_count;
    passed_times = g_test_context.all_test_cases_count - failed_times;

    if (failed_times > 0)
    {
        QM_LOGD(LOG_TAG, "total:%d test cases, passed:%d failed:%d\n",
               g_test_context.all_test_cases_count, passed_times, failed_times);
    }
    else
    {
        QM_LOGD(LOG_TAG, "total:%d test cases, passed:%d\n",
               g_test_context.all_test_cases_count, passed_times);

    }

    cunit_test_case_result_t *result = g_test_context.failed_test_case_result.next;
    while (result != NULL)
    {
        QM_LOGD(LOG_TAG, "failed at %s(#%d)  %s", result->file, result->line, result->msg);
        result = result->next;
    }

    QM_LOGD(LOG_TAG, "\n--------------------------------\n");
}

#ifndef JOYINSIDE_TEST_SUPPORT_H
#define JOYINSIDE_TEST_SUPPORT_H
#include "qm_websocket_client.h"
#include "qm_kernel.h"
#define TEST_WIRE_CAPACITY 12000U
extern char test_http_body[TEST_WIRE_CAPACITY];
extern char test_http_first[TEST_WIRE_CAPACITY];
extern char test_http_url[512];
extern char test_http_authorization[512];
extern const char *test_http_reply;
extern int test_http_calls;
extern int test_http_failures;
extern int test_ws_starts;
extern int test_ws_reconnects;
extern int test_send_result;
extern char test_ws_uri[4096];
extern char test_ws_headers[8192];
extern char test_ws_sent[TEST_WIRE_CAPACITY];
extern qm_bool test_signal_sends;
extern qm_sem_t test_send_sem;
extern qm_bool test_signal_start;
extern qm_sem_t test_start_sem;
unsigned test_book_count(const char *event);
void test_book_last(const char *event, char *output, unsigned capacity);
#endif

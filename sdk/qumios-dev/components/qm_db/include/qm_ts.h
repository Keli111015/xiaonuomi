#ifndef QM_TS_H
#define QM_TS_H

#include "qm_db_log.h"

#ifdef __cplusplus
extern "C"{
#endif

#include "qm_errno.h"
#include "qm_types.h"

#define QM_TS_STATUS_NUM      6

typedef enum qm_ts_status {
    QM_TS_UNUSED,
    QM_TS_PRE_WRITE,
    QM_TS_WRITE,
    QM_TS_USER_STATUS1,
    QM_TS_DELETED,
    QM_TS_USER_STATUS2,
}qm_ts_status_t;

typedef struct qm_ts{
    qm_ts_status_t status;      /**< node status, @see qm_ts_status_t */
    uint32_t time;              /**< node timestamp */
    uint32_t len;               /**< data length */
    uint32_t addr;              /**< node address */
    uint32_t index;             /**< node index address */
}qm_ts_t;

typedef bool_t (*qm_ts_cb)(qm_ts_t *ts, void *arg);
typedef uint32_t (*qm_ts_get_time)(void);

void *qm_ts_init(const char *name, qm_ts_get_time get_time, uint32_t max_len);
int qm_ts_clean(void *db);

int qm_ts_append(void *db, const void *buf, uint32_t len);
int qm_ts_read(void *db, qm_ts_t *ts, void *buf, uint32_t len);

int qm_ts_iter(void *db, qm_ts_cb cb, void *arg);
int qm_ts_iter_reverse(void *db, qm_ts_cb cb, void *arg);
int qm_ts_iter_by_status(void *db, qm_ts_status_t status, qm_ts_cb cb, void *arg);
int qm_ts_iter_reverse_by_status(void *db, qm_ts_status_t status, qm_ts_cb cb, void *arg);
int qm_ts_iter_by_time(void *db, uint32_t from, uint32_t to, qm_ts_cb cb, void *arg);
int qm_ts_iter_by_time_and_status(void *db, uint32_t from, uint32_t to, qm_ts_status_t status, qm_ts_cb cb, void *arg);
int qm_ts_query_count(void *db, uint32_t from, uint32_t to, qm_ts_status_t status);
int qm_ts_set_status(void *db, qm_ts_t *ts, qm_ts_status_t status);



#ifdef __cplusplus
}
#endif

#endif /* QM_TS_H */

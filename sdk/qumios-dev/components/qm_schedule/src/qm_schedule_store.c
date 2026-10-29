#include "qm_schedule_internal.h"

#include "qm_kv.h"

#define QM_SCHEDULE_KV_ACTIVE "qs/active/v2"
#define QM_SCHEDULE_KV_BANK0  "qs/bank/0/v2"
#define QM_SCHEDULE_KV_BANK1  "qs/bank/1/v2"
#define QM_SCHEDULE_KV_LEGACY "qs/blob/v1"

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t active_bank;
    uint8_t reserved[3];
    uint32_t sequence;
    uint32_t checksum;
} qm_schedule_store_active_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    qm_schedule_store_t store;
    uint32_t checksum;
} qm_schedule_store_blob_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    qm_schedule_store_t store;
    uint32_t checksum;
} qm_schedule_store_legacy_blob_t;

static uint32_t checksum_bytes(const uint8_t *data, uint32_t len)
{
    uint32_t hash = 2166136261u;
    uint32_t i;

    for (i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void init_empty_store(qm_schedule_store_t *store)
{
    memset(store, 0, sizeof(*store));
    store->meta.format_version = QM_SCHEDULE_STORE_VERSION;
    store->meta.sync_state = QM_SCHEDULE_SYNC_NOT_REQUIRED;
}

static uint32_t blob_checksum(const qm_schedule_store_blob_t *blob)
{
    uint32_t hash = checksum_bytes((const uint8_t *)&blob->sequence,
                                   (uint32_t)sizeof(blob->sequence));

    hash ^= checksum_bytes((const uint8_t *)&blob->store,
                           (uint32_t)sizeof(blob->store));
    return hash;
}

static uint32_t active_checksum(const qm_schedule_store_active_t *active)
{
    return checksum_bytes((const uint8_t *)active,
                          (uint32_t)(sizeof(*active) -
                                     sizeof(active->checksum)));
}

static const char *bank_key(uint8_t bank)
{
    return bank == 0 ? QM_SCHEDULE_KV_BANK0 : QM_SCHEDULE_KV_BANK1;
}

static int load_active(qm_schedule_store_active_t *active)
{
    int len = (int)sizeof(*active);
    int ret;

    memset(active, 0, sizeof(*active));
    ret = qm_kv_get(QM_SCHEDULE_KV_ACTIVE, active, &len);
    if (ret != QM_EOK) {
        return ret;
    }
    if (len != (int)sizeof(*active) ||
        active->magic != QM_SCHEDULE_STORE_MAGIC ||
        active->version != QM_SCHEDULE_STORE_VERSION ||
        active->active_bank > 1 ||
        active->checksum != active_checksum(active)) {
        return -QM_EINVAL;
    }
    return QM_EOK;
}

static int load_bank(uint8_t bank, qm_schedule_store_blob_t *blob)
{
    int len = (int)sizeof(*blob);
    int ret;

    memset(blob, 0, sizeof(*blob));
    ret = qm_kv_get(bank_key(bank), blob, &len);
    if (ret != QM_EOK) {
        return ret;
    }
    if (len != (int)sizeof(*blob) ||
        blob->magic != QM_SCHEDULE_STORE_MAGIC ||
        blob->version != QM_SCHEDULE_STORE_VERSION ||
        blob->checksum != blob_checksum(blob)) {
        return -QM_EINVAL;
    }
    return QM_EOK;
}

static int load_legacy(qm_schedule_store_blob_t *blob)
{
    qm_schedule_store_legacy_blob_t *legacy;
    int len;
    int ret;

    memset(blob, 0, sizeof(*blob));
    legacy = (qm_schedule_store_legacy_blob_t *)qm_malloc(sizeof(*legacy));
    if (!legacy) {
        return -QM_ENOMEM;
    }
    memset(legacy, 0, sizeof(*legacy));
    len = (int)sizeof(*legacy);
    ret = qm_kv_get(QM_SCHEDULE_KV_LEGACY, legacy, &len);
    if (ret != QM_EOK) {
        qm_free(legacy);
        return ret;
    }
    if (len != (int)sizeof(*legacy) ||
        legacy->magic != QM_SCHEDULE_STORE_MAGIC ||
        legacy->version != QM_SCHEDULE_STORE_VERSION ||
        legacy->checksum != checksum_bytes((const uint8_t *)&legacy->store,
                                           (uint32_t)sizeof(legacy->store))) {
        qm_free(legacy);
        return -QM_EINVAL;
    }
    blob->magic = legacy->magic;
    blob->version = legacy->version;
    blob->sequence = 0;
    blob->store = legacy->store;
    blob->checksum = blob_checksum(blob);
    qm_free(legacy);
    return QM_EOK;
}

int qm_schedule_store_init(qm_schedule_t schedule)
{
    if (!schedule) {
        return -QM_EINVAL;
    }

    init_empty_store(&schedule->store);
    schedule->active_bank = 0;
    schedule->store_sequence = 0;
    return QM_EOK;
}

void qm_schedule_store_deinit(qm_schedule_t schedule)
{
    (void)schedule;
}

int qm_schedule_store_recover(qm_schedule_t schedule)
{
    qm_schedule_store_active_t active;
    qm_schedule_store_blob_t *blob;
    qm_schedule_store_blob_t *fallback;
    int ret;
    uint8_t selected_bank = 0;

    if (!schedule) {
        return -QM_EINVAL;
    }

    blob = (qm_schedule_store_blob_t *)qm_malloc(sizeof(*blob));
    if (!blob) {
        return -QM_ENOMEM;
    }
    fallback = (qm_schedule_store_blob_t *)qm_malloc(sizeof(*fallback));
    if (!fallback) {
        qm_free(blob);
        return -QM_ENOMEM;
    }

    ret = load_active(&active);
    if (ret == QM_EOK && load_bank(active.active_bank, blob) == QM_EOK) {
        selected_bank = active.active_bank;
    } else {
        int ret0 = load_bank(0, blob);
        int ret1 = load_bank(1, fallback);

        if (ret0 == QM_EOK && ret1 == QM_EOK) {
            if (fallback->sequence > blob->sequence) {
                *blob = *fallback;
                selected_bank = 1;
            }
        } else if (ret0 == QM_EOK) {
            selected_bank = 0;
        } else if (ret1 == QM_EOK) {
            *blob = *fallback;
            selected_bank = 1;
        } else if (load_legacy(blob) == QM_EOK) {
            selected_bank = 0;
        } else {
            qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
            schedule->store.meta.recovery_pending = true;
            schedule->store.meta.confirmed_revision = 0;
            schedule->store.meta.sync_state = QM_SCHEDULE_SYNC_PENDING;
            qm_mutex_unlock(&schedule->lock);
            qm_free(fallback);
            qm_free(blob);
            return qm_schedule_store_save_all(schedule);
        }
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    schedule->store = blob->store;
    schedule->active_bank = selected_bank;
    schedule->store_sequence = blob->sequence;
    schedule->schedulable = false;
    qm_mutex_unlock(&schedule->lock);
    qm_free(fallback);
    qm_free(blob);
    return QM_EOK;
}

int qm_schedule_store_load_meta(qm_schedule_t schedule,
                                qm_schedule_store_meta_t *meta)
{
    if (!schedule || !meta) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    *meta = schedule->store.meta;
    qm_mutex_unlock(&schedule->lock);
    return QM_EOK;
}

int qm_schedule_store_save_meta(qm_schedule_t schedule,
                                const qm_schedule_store_meta_t *meta)
{
    if (!schedule || !meta) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    schedule->store.meta = *meta;
    qm_mutex_unlock(&schedule->lock);
    return qm_schedule_store_save_all(schedule);
}

int qm_schedule_store_save_all(qm_schedule_t schedule)
{
    qm_schedule_store_active_t active;
    qm_schedule_store_blob_t *blob;
    uint8_t next_bank;
    uint32_t next_sequence;
    int ret;

    if (!schedule) {
        return -QM_EINVAL;
    }

    blob = (qm_schedule_store_blob_t *)qm_malloc(sizeof(*blob));
    if (!blob) {
        return -QM_ENOMEM;
    }

    memset(blob, 0, sizeof(*blob));
    blob->magic = QM_SCHEDULE_STORE_MAGIC;
    blob->version = QM_SCHEDULE_STORE_VERSION;
    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    next_bank = schedule->active_bank == 0 ? 1 : 0;
    next_sequence = schedule->store_sequence + 1;
    blob->sequence = next_sequence;
    blob->store = schedule->store;
    qm_mutex_unlock(&schedule->lock);
    blob->checksum = blob_checksum(blob);

    ret = qm_kv_set(bank_key(next_bank), blob, (int)sizeof(*blob), 1);
    if (ret != QM_EOK) {
        qm_free(blob);
        return ret;
    }

    memset(&active, 0, sizeof(active));
    active.magic = QM_SCHEDULE_STORE_MAGIC;
    active.version = QM_SCHEDULE_STORE_VERSION;
    active.active_bank = next_bank;
    active.sequence = next_sequence;
    active.checksum = active_checksum(&active);
    ret = qm_kv_set(QM_SCHEDULE_KV_ACTIVE, &active, (int)sizeof(active), 1);
    if (ret == QM_EOK) {
        qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
        schedule->active_bank = next_bank;
        schedule->store_sequence = next_sequence;
        qm_mutex_unlock(&schedule->lock);
    }
    qm_free(blob);
    return ret;
}

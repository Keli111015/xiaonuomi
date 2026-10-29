/**
 * @file mdns.c
 * @brief QUMI UDP implementation of synchronous DNS-SD discovery.
 */
#include "mdns.h"
#include "qm_config.h"
#include "qm_errno.h"
#include "qmos/qm_kernel.h"
#include "qm_string.h"
#include "qm_udp.h"

#ifndef CONFIG_MDNS_PACKET_SIZE
#define CONFIG_MDNS_PACKET_SIZE 1500U
#endif
#if CONFIG_MDNS_PACKET_SIZE < 1500U
#error "CONFIG_MDNS_PACKET_SIZE must be at least 1500 bytes"
#endif

#define MDNS_PORT 5353U
#define MDNS_MULTICAST "224.0.0.251"
#define MDNS_MAX_LABEL 63U
#define MDNS_MAX_WIRE_NAME_LEN 255U
#define MDNS_MAX_SERVICES 4U
#define MDNS_MAX_A_RECORDS 4U
#define MDNS_MAX_PACKETS 3U
#define MDNS_TAIL_TIMEOUT_MS 100U
#define MDNS_IPV4_TEXT_LEN 16U

#define MDNS_HDR_FLAGS 2U
#define MDNS_HDR_QDCOUNT 5U
#define MDNS_TYPE_A 1U
#define MDNS_TYPE_PTR 12U
#define MDNS_TYPE_TXT 16U
#define MDNS_TYPE_SRV 33U
#define MDNS_CLASS_IN 1U
#define MDNS_CLASS_UNICAST_RESPONSE 0x8000U
#define MDNS_FLAG_RESPONSE 0x80U
#define MDNS_FLAG_TRUNCATED 0x02U
#define MDNS_OPCODE_MASK 0x78U
#define MDNS_RCODE_MASK 0x0fU
#define MDNS_CLASS_MASK 0x7fffU

typedef struct {
    mdns_service_t svc;
    uint8_t used;
    uint8_t has_ptr;
    uint8_t has_srv;
} mdns_entry_t;

typedef struct {
    char name[MDNS_NAME_LEN];
    char ip[MDNS_IPV4_TEXT_LEN];
    uint8_t used;
} mdns_a_record_t;

typedef struct {
    mdns_entry_t entries[MDNS_MAX_SERVICES];
    mdns_a_record_t a_records[MDNS_MAX_A_RECORDS];
    uint8_t packet[];
} mdns_query_storage_t;

static uint16_t read_u16(const uint8_t *packet, size_t offset)
{
    return (uint16_t)(((uint16_t)packet[offset] << 8) | packet[offset + 1U]);
}

static uint32_t read_u32(const uint8_t *packet, size_t offset)
{
    return ((uint32_t)packet[offset] << 24) | ((uint32_t)packet[offset + 1U] << 16) |
           ((uint32_t)packet[offset + 2U] << 8) | packet[offset + 3U];
}

/* Render an IPv4 A record into the public host string without libc formatting. */
static void format_ipv4(const uint8_t *address, char *output, size_t capacity)
{
    size_t pos = 0U;
    unsigned i;
    for (i = 0U; i < 4U && pos + 1U < capacity; ++i) {
        uint8_t value = address[i];
        char digits[3];
        size_t count = 0U, j;
        do {
            digits[count++] = (char)('0' + value % 10U);
            value = (uint8_t)(value / 10U);
        } while (value && count < sizeof(digits));
        for (j = 0U; j < count && pos + 1U < capacity; ++j)
            output[pos++] = digits[count - j - 1U];
        if (i != 3U && pos + 1U < capacity)
            output[pos++] = '.';
    }
    if (capacity)
        output[pos < capacity ? pos : capacity - 1U] = '\0';
}

static void copy_str(char *dst, const char *src, size_t capacity)
{
    size_t len;
    if (!dst || !src || capacity == 0U)
        return;
    len = qm_strlen(src);
    if (len >= capacity)
        len = capacity - 1U;
    qm_memcpy(dst, src, (uint32_t)len);
    dst[len] = '\0';
}

/* DNS names are compared case-insensitively and tolerate a trailing root dot. */
static int name_equals(const char *a, const char *b)
{
    size_t a_len, b_len;
    if (!a || !b)
        return 0;
    a_len = qm_strlen(a);
    b_len = qm_strlen(b);
    while (a_len > 0U && a[a_len - 1U] == '.')
        --a_len;
    while (b_len > 0U && b[b_len - 1U] == '.')
        --b_len;
    if (a_len != b_len)
        return 0;
    return qm_strncasecmp(a, b, (uint32_t)a_len) == 0;
}

/* An SRV/TXT owner is a candidate only when it ends in the queried service type. */
static int instance_matches_service(const char *instance, const char *service_type)
{
    size_t instance_len;
    size_t service_len;

    if (!instance || !service_type)
        return 0;
    instance_len = qm_strlen(instance);
    service_len = qm_strlen(service_type);
    while (instance_len > 0U && instance[instance_len - 1U] == '.')
        --instance_len;
    while (service_len > 0U && service_type[service_len - 1U] == '.')
        --service_len;
    if (instance_len <= service_len || instance[instance_len - service_len - 1U] != '.')
        return 0;
    return qm_strncasecmp(instance + instance_len - service_len, service_type,
                          (uint32_t)service_len) == 0;
}

/* Decode labels and compression pointers while preserving the consumed offset. */
static int read_name(const uint8_t *packet, size_t length, size_t *offset, char *output,
                     size_t capacity)
{
    size_t pos = *offset, end = pos, out = 0U;
    unsigned jumps = 0U;
    int jumped = 0;
    if (!output || capacity == 0U)
        return -QM_EINVAL;
    /* Compression pointers are followed without changing the consumed offset. */
    while (pos < length) {
        uint8_t size = packet[pos++];
        if (size == 0U) {
            if (!jumped)
                end = pos;
            if (out >= capacity)
                return -QM_EFULL;
            output[out] = '\0';
            *offset = end;
            return QM_EOK;
        }
        if ((size & 0xc0U) == 0xc0U) {
            uint16_t target;
            size_t pointer_offset = pos - 1U;
            if (pos >= length)
                return -QM_EINVAL;
            target = (uint16_t)(((size & 0x3fU) << 8) | packet[pos++]);
            if (target >= pointer_offset || ++jumps > MDNS_NAME_LEN)
                return -QM_EINVAL;
            if (!jumped)
                end = pos;
            pos = target;
            jumped = 1;
            continue;
        }
        if (size > MDNS_MAX_LABEL || pos + size > length)
            return -QM_EINVAL;
        if (out + size + (out ? 1U : 0U) >= capacity)
            return -QM_EFULL;
        if (out)
            output[out++] = '.';
        qm_memcpy(output + out, packet + pos, size);
        out += size;
        pos += size;
    }
    return -QM_EINVAL;
}

static int encode_name(uint8_t *packet, size_t capacity, size_t *offset, const char *name)
{
    const char *start = name, *dot;
    size_t wire_length = 1U;

    while (start && *start) {
        size_t label_len;
        dot = qm_strchr(start, '.');
        label_len = dot ? (size_t)(dot - start) : (size_t)qm_strlen(start);
        if (label_len == 0U || label_len > MDNS_MAX_LABEL ||
            wire_length + label_len + 1U > MDNS_MAX_WIRE_NAME_LEN)
            return -QM_EINVAL;
        if (*offset + label_len + 1U >= capacity)
            return -QM_EFULL;
        packet[(*offset)++] = (uint8_t)label_len;
        qm_memcpy(packet + *offset, start, (uint32_t)label_len);
        *offset += label_len;
        wire_length += label_len + 1U;
        if (!dot)
            break;
        start = dot + 1;
    }
    if (*offset >= capacity)
        return -QM_EFULL;
    packet[(*offset)++] = 0U;
    return QM_EOK;
}

static void copy_txt(const uint8_t *data, size_t length, mdns_service_t *service)
{
    if (length > sizeof(service->txt)) {
        service->txt_len = 0U;
        service->txt_truncated = 1U;
        return;
    }
    qm_memcpy(service->txt, data, (uint32_t)length);
    service->txt_len = (uint16_t)length;
    service->txt_truncated = 0U;
}

static mdns_entry_t *find_entry(mdns_entry_t *entries, const char *name, int replace_pending)
{
    size_t i;
    for (i = 0U; i < MDNS_MAX_SERVICES; ++i) {
        if (entries[i].used && name_equals(entries[i].svc.instance, name))
            return &entries[i];
    }
    for (i = 0U; i < MDNS_MAX_SERVICES; ++i) {
        if (!entries[i].used) {
            goto initialize;
        }
    }
    if (!replace_pending)
        return NULL;
    for (i = 0U; i < MDNS_MAX_SERVICES; ++i) {
        if (!entries[i].has_ptr)
            goto initialize;
    }
    return NULL;

initialize:
    qm_memset(&entries[i], 0, sizeof(entries[i]));
    entries[i].used = 1U;
    copy_str(entries[i].svc.instance, name, sizeof(entries[i].svc.instance));
    return &entries[i];
}

static mdns_entry_t *lookup_entry(mdns_entry_t *entries, const char *name)
{
    size_t i;
    for (i = 0U; i < MDNS_MAX_SERVICES; ++i) {
        if (entries[i].used && name_equals(entries[i].svc.instance, name))
            return &entries[i];
    }
    return NULL;
}

/* Keep a bounded A-record table so SRV targets can be resolved after parsing. */
static mdns_a_record_t *find_a_record(mdns_a_record_t *records, const char *name)
{
    size_t i;
    for (i = 0U; i < MDNS_MAX_A_RECORDS; ++i) {
        if (records[i].used && name_equals(records[i].name, name))
            return &records[i];
    }
    for (i = 0U; i < MDNS_MAX_A_RECORDS; ++i) {
        if (!records[i].used) {
            records[i].used = 1U;
            copy_str(records[i].name, name, sizeof(records[i].name));
            return &records[i];
        }
    }
    return NULL;
}

static mdns_a_record_t *lookup_a_record(mdns_a_record_t *records, const char *name)
{
    size_t i;
    for (i = 0U; i < MDNS_MAX_A_RECORDS; ++i) {
        if (records[i].used && name_equals(records[i].name, name))
            return &records[i];
    }
    return NULL;
}

static int validate_txt_rdata(const uint8_t *data, size_t length)
{
    size_t pos = 0U;
    while (pos < length) {
        size_t item_length = data[pos++];
        if (pos + item_length > length)
            return -QM_EINVAL;
        pos += item_length;
    }
    return QM_EOK;
}

/* Validate every record before changing the cross-packet aggregate. */
static int validate_packet(const uint8_t *packet, size_t length, char *name, size_t name_capacity)
{
    size_t pos = 12U;
    uint32_t record_count, i;
    uint16_t flags;

    if (!name || name_capacity == 0U || length < 12U || packet[0] != 0U || packet[1] != 0U)
        return -QM_EINVAL;
    flags = read_u16(packet, MDNS_HDR_FLAGS);
    if ((flags & (uint16_t)(MDNS_FLAG_RESPONSE << 8)) == 0U ||
        (packet[MDNS_HDR_FLAGS] & MDNS_OPCODE_MASK) != 0U ||
        (packet[MDNS_HDR_FLAGS + 1U] & (MDNS_FLAG_TRUNCATED | MDNS_RCODE_MASK)) != 0U)
        return -QM_EINVAL;
    for (i = 0U; i < read_u16(packet, 4U); ++i) {
        if (read_name(packet, length, &pos, name, name_capacity) != QM_EOK || pos + 4U > length)
            return -QM_EINVAL;
        pos += 4U;
    }
    record_count = (uint32_t)read_u16(packet, 6U) + (uint32_t)read_u16(packet, 8U) +
                   (uint32_t)read_u16(packet, 10U);
    for (i = 0U; i < record_count; ++i) {
        uint16_t type, data_len;
        size_t data_start;
        if (read_name(packet, length, &pos, name, name_capacity) != QM_EOK || pos + 10U > length)
            return -QM_EINVAL;
        type = read_u16(packet, pos);
        data_len = read_u16(packet, pos + 8U);
        data_start = pos + 10U;
        if (data_start + data_len > length)
            return -QM_EINVAL;
        if (type == MDNS_TYPE_PTR) {
            size_t data_end = data_start;
            if (read_name(packet, length, &data_end, name, name_capacity) != QM_EOK ||
                data_end != data_start + data_len)
                return -QM_EINVAL;
        } else if (type == MDNS_TYPE_SRV) {
            size_t target = data_start + 6U;
            if (data_len < 6U || read_name(packet, length, &target, name, name_capacity) != QM_EOK ||
                target != data_start + data_len)
                return -QM_EINVAL;
        } else if (type == MDNS_TYPE_TXT && validate_txt_rdata(packet + data_start, data_len) !=
                                      QM_EOK) {
            return -QM_EINVAL;
        }
        pos = data_start + data_len;
    }
    return pos == length ? QM_EOK : -QM_EINVAL;
}

static int parse_packet(const uint8_t *packet, size_t length, const char *service_type,
                        mdns_entry_t *entries, mdns_a_record_t *a_records)
{
    /* PTR/SRV/TXT/A records may arrive in any order, so collect before callbacks. */
    size_t pos = 12U;
    uint32_t record_count, i;
    char owner[MDNS_NAME_LEN];
    if (validate_packet(packet, length, owner, sizeof(owner)) != QM_EOK)
        return -QM_EINVAL;
    for (i = 0U; i < read_u16(packet, 4U); ++i) {
        if (read_name(packet, length, &pos, owner, sizeof(owner)) != QM_EOK ||
            pos + 4U > length)
            return -QM_EINVAL;
        pos += 4U;
    }
    record_count = (uint32_t)read_u16(packet, 6U) + (uint32_t)read_u16(packet, 8U) +
                   (uint32_t)read_u16(packet, 10U);
    for (i = 0U; i < record_count; ++i) {
        uint16_t type, record_class, data_len;
        uint32_t ttl;
        if (read_name(packet, length, &pos, owner, sizeof(owner)) != QM_EOK || pos + 10U > length)
            return -QM_EINVAL;
        type = read_u16(packet, pos);
        record_class = (uint16_t)(read_u16(packet, pos + 2U) & MDNS_CLASS_MASK);
        ttl = read_u32(packet, pos + 4U);
        data_len = read_u16(packet, pos + 8U);
        pos += 10U;
        if (pos + data_len > length)
            return -QM_EINVAL;
        if (record_class != MDNS_CLASS_IN)
            goto next_record;
        if (type == MDNS_TYPE_PTR) {
            size_t saved = pos;
            mdns_entry_t *entry;
            if (!name_equals(owner, service_type) ||
                read_name(packet, length, &saved, owner, sizeof(owner)) != QM_EOK ||
                saved != pos + data_len)
                goto next_record;
            entry = ttl == 0U ? lookup_entry(entries, owner) : find_entry(entries, owner, 1);
            if (entry)
                entry->has_ptr = ttl != 0U;
        } else if (type == MDNS_TYPE_SRV && data_len >= 6U) {
            mdns_entry_t *entry = lookup_entry(entries, owner);
            if (!entry && ttl != 0U && instance_matches_service(owner, service_type))
                entry = find_entry(entries, owner, 0);
            if (entry && ttl == 0U) {
                entry->has_srv = 0U;
                entry->svc.port = 0U;
                entry->svc.host[0] = '\0';
            } else if (entry) {
                size_t saved = pos + 6U;
                entry->svc.port = read_u16(packet, pos + 4U);
                if (read_name(packet, length, &saved, entry->svc.host, sizeof(entry->svc.host)) ==
                    QM_EOK &&
                    saved == pos + data_len)
                    entry->has_srv = 1U;
                else
                    entry->svc.port = 0U;
            }
        } else if (type == MDNS_TYPE_TXT) {
            mdns_entry_t *entry = lookup_entry(entries, owner);
            if (!entry && ttl != 0U && instance_matches_service(owner, service_type))
                entry = find_entry(entries, owner, 0);
            if (entry && ttl == 0U) {
                entry->svc.txt_len = 0U;
                entry->svc.txt_truncated = 0U;
            } else if (entry) {
                copy_txt(packet + pos, data_len, &entry->svc);
            }
        } else if (type == MDNS_TYPE_A && data_len == 4U) {
            mdns_a_record_t *record =
                ttl == 0U ? lookup_a_record(a_records, owner) : find_a_record(a_records, owner);
            if (record && ttl == 0U) {
                qm_memset(record, 0, sizeof(*record));
            } else if (record) {
                format_ipv4(packet + pos, record->ip, sizeof(record->ip));
            }
        }
    next_record:
        pos += data_len;
    }
    return QM_EOK;
}

int mdns_txt_get(const mdns_service_t *service, const char *key, char *value, uint16_t capacity)
{
    size_t pos = 0U;
    size_t key_len;
    if (!service || !key || !key[0] || !value || capacity == 0U)
        return -QM_EINVAL;
    if (service->txt_len > sizeof(service->txt))
        return -QM_EINVAL;
    if (service->txt_truncated)
        return -QM_EFULL;
    key_len = qm_strlen(key);
    while (pos < service->txt_len) {
        uint8_t item_len = service->txt[pos++];
        const uint8_t *item;
        const uint8_t *equal;
        size_t value_len;
        if (pos + item_len > service->txt_len)
            return -QM_EINVAL;
        if (item_len == 0U)
            continue;
        item = service->txt + pos;
        equal = (const uint8_t *)qm_memchr(item, '=', item_len);
        if ((!equal && item_len == key_len) || (equal && (size_t)(equal - item) == key_len)) {
            if (qm_strncasecmp((const char *)item, key, (uint32_t)key_len) != 0) {
                pos += item_len;
                continue;
            }
            if (!equal) {
                value[0] = '\0';
                return QM_EOK;
            }
            value_len = item_len - key_len - 1U;
            if (value_len >= capacity)
                return -QM_EFULL;
            qm_memcpy(value, equal + 1U, (uint32_t)value_len);
            value[value_len] = '\0';
            return QM_EOK;
        }
        pos += item_len;
    }
    return -QM_ENOENT;
}

/** Send one PTR query and report all complete service endpoints synchronously. */
int mdns_query(const char *service_type, uint32_t timeout_ms, mdns_service_cb_t callback,
               void *user)
{
    size_t query_len = 12U, i;
    mdns_query_storage_t *storage = NULL;
    qm_netaddr_t multicast;
    int socket = -1;
    int received;
    int result = QM_EOK;
    int found = 0;
    unsigned packet_count;
    unsigned valid_packets = 0U;

    if (!service_type || !*service_type || !callback || timeout_ms == 0U)
        return -QM_EINVAL;

    storage = (mdns_query_storage_t *)qm_malloc(sizeof(*storage) + CONFIG_MDNS_PACKET_SIZE);
    if (!storage)
        return -QM_ENOMEM;
    qm_memset(storage, 0, sizeof(*storage));

    qm_memset(storage->packet, 0, 12U);
    storage->packet[MDNS_HDR_QDCOUNT] = 1U;
    if (encode_name(storage->packet, CONFIG_MDNS_PACKET_SIZE, &query_len, service_type) != QM_EOK ||
        query_len + 4U > CONFIG_MDNS_PACKET_SIZE) {
        result = -QM_EINVAL;
        goto exit;
    }
    storage->packet[query_len++] = 0U;
    storage->packet[query_len++] = MDNS_TYPE_PTR;
    /* Ask responders to unicast the answer to this ephemeral source port. */
    storage->packet[query_len++] = (uint8_t)((MDNS_CLASS_UNICAST_RESPONSE | MDNS_CLASS_IN) >> 8);
    storage->packet[query_len++] = (uint8_t)(MDNS_CLASS_UNICAST_RESPONSE | MDNS_CLASS_IN);

    qm_memset(&multicast, 0, sizeof(multicast));
    copy_str(multicast.addr, MDNS_MULTICAST, sizeof(multicast.addr));
    multicast.port = MDNS_PORT;

    /*
     * Bind an ephemeral port and use sendto/recvfrom instead of a socket that
     * is connected to the multicast address. The QU bit above requests
     * unicast responses, while a connected datagram socket would only accept
     * replies whose source is 224.0.0.251.
     */
    socket = qm_udp_server_create(NULL, 0);
    if (socket < 0) {
        result = -QM_EIO;
        goto exit;
    }
    received = qm_udp_sendto(socket, (const char *)storage->packet, (uint32_t)query_len,
                              &multicast, timeout_ms);
    if (received != (int)query_len) {
        result = received < 0 ? received : -QM_EIO;
        goto exit;
    }

    /* Collect a short response tail because different responders answer independently. */
    for (packet_count = 0U; packet_count < MDNS_MAX_PACKETS; ++packet_count) {
        uint32_t wait_ms = packet_count == 0U ? timeout_ms : MDNS_TAIL_TIMEOUT_MS;
        received = qm_udp_recvfrom(socket, (char *)storage->packet, CONFIG_MDNS_PACKET_SIZE,
                                   &multicast, wait_ms);
        if (received < 0) {
            if (valid_packets == 0U && result == QM_EOK)
                result = received;
            break;
        }
        if (received < 12 || multicast.port != MDNS_PORT ||
            parse_packet(storage->packet, (size_t)received, service_type, storage->entries,
                         storage->a_records) != QM_EOK) {
            continue;
        }
        ++valid_packets;
        result = QM_EOK;
    }

    for (i = 0U; i < MDNS_MAX_SERVICES; ++i) {
        mdns_entry_t *entry = &storage->entries[i];
        size_t j;
        if (!entry->used || !entry->has_ptr || !entry->has_srv || !entry->svc.port)
            continue;
        if (entry->svc.host[0]) {
            for (j = 0U; j < MDNS_MAX_A_RECORDS; ++j) {
                if (storage->a_records[j].used &&
                    name_equals(storage->a_records[j].name, entry->svc.host)) {
                    copy_str(entry->svc.host, storage->a_records[j].ip, sizeof(entry->svc.host));
                    break;
                }
            }
        }
        if (!entry->svc.host[0])
            continue;
        found = 1;
        result = callback(&entry->svc, user);
        if (result != QM_EOK)
            break;
    }

exit:
    if (socket >= 0)
        (void)qm_udp_close(socket);
    qm_free(storage);
    if (found)
        return result;
    if (valid_packets > 0U)
        return -QM_ENOENT;
    return result != QM_EOK ? result : -QM_ENOENT;
}

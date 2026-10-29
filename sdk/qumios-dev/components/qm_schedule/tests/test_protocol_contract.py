#!/usr/bin/env python3
"""Contract checks that run without an ESP-IDF toolchain."""

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IOT_SOURCE = (ROOT / "src" / "qm_schedule_iot.c").read_text()
README = (ROOT / "README.md").read_text()
README_CN = (ROOT / "README_CN.md").read_text()
KCONFIG = (ROOT / "qm_kconfig").read_text()
PUBLIC_HEADER = (ROOT / "include" / "qm_schedule.h").read_text()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"),
                      sort_keys=True).encode("utf-8")


def test_jcs_vectors():
    group_clear = {
        "chunkCount": 1,
        "chunkIndex": 0,
        "desiredRevision": 12,
        "groupId": "g-1",
        "groupRevision": 3,
        "replace": True,
        "schedules": [],
        "syncId": "sync-1",
        "syncScope": "group",
        "totalItems": 0,
    }
    full_projection = {
        "chunkCount": 1,
        "chunkIndex": 0,
        "desiredRevision": 21,
        "groups": [],
        "replace": True,
        "schedules": [{
            "enabled": True,
            "extensions": {
                "demo": {
                    "label": '\u670d\u836f "A" \\ \u665a',
                    "schemaVersion": 1,
                }
            },
            "id": "med-1",
            "recurrence": None,
            "reminders": [],
            "revision": 2,
            "source": "remote",
            "startAt": 1785826800,
            "timezone": "GMT+08:00",
            "title": '\u665a\u95f4\u670d\u836f',
            "type": "medication",
        }],
        "syncId": "sync-2",
        "syncScope": "fullProjection",
        "totalItems": 1,
    }
    assert hashlib.sha256(canonical(group_clear)).hexdigest() == \
        "93791e3461198afb95c310710e60f2d98aba20f6b7374188aa70f93ef7669898"
    assert hashlib.sha256(canonical(full_projection)).hexdigest() == \
        "2efb17662673437da107b1874c8754da001fffd697258b0c83b6f64660c7ae99"


def test_wire_and_retry_contract_is_in_source():
    assert '"method\\":\\"scheduleEvent' not in IOT_SOURCE
    assert '"method\\":\\"scheduleReply' in IOT_SOURCE
    assert '"syncScope"' in IOT_SOURCE
    assert 'verify_sync_digest' in IOT_SOURCE
    assert 'awaiting_ack' in IOT_SOURCE
    assert 'CONFIG_QM_SCHEDULE_OUTBOX_ACK_TIMEOUT_SEC' in IOT_SOURCE
    assert 'QM_SCHEDULE_E_REVISION_CONFLICT' in IOT_SOURCE
    assert 'QM_SCHEDULE_MAX_SYNC_ITEMS_PER_CHUNK_INTERNAL' in IOT_SOURCE
    assert 'maxSyncItemsPerChunk' in IOT_SOURCE


def test_frozen_capacity_contract():
    assert 'config QM_SCHEDULE_SYNC_MAX_ITEMS_PER_CHUNK' in KCONFIG
    assert 'default 20' in KCONFIG
    assert 'max_sync_items_per_chunk' in PUBLIC_HEADER
    assert '| Sync items per chunk | 20 |' in README


def test_docs_freeze_the_same_baseline():
    assert "Frozen V0.1 Baseline" in README
    assert "已冻结决策基线" in (ROOT / "PRD_CN.md").read_text()
    assert "scheduleEvent" not in IOT_SOURCE
    assert "contentDigest" in README
    assert "30-second event ACK timeout" in README
    assert "2efb17662673437da107b1874c8754da001fffd697258b0c83b6f64660c7ae99" in README_CN


if __name__ == "__main__":
    test_jcs_vectors()
    test_wire_and_retry_contract_is_in_source()
    test_docs_freeze_the_same_baseline()
    print("qm_schedule protocol contract: PASS")

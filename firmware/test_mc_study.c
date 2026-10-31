#include "mc_study.h"
#include <assert.h>
#include <stdio.h>

static void test_initial_and_chat_penalties(void)
{
    mc_study s;
    mc_study_reset(&s);
    assert(s.hp == 100 && s.hp_tenths == 1000 && !s.active && !s.exhausted);

    /* Inactive session does not penalize chat */
    assert(!mc_study_chat_detected(&s, 1000));
    assert(s.hp == 100 && s.hp_tenths == 1000 && s.penalties == 0);

    /* Start focus */
    mc_study_set_active(&s, true);
    assert(s.active && !s.exhausted);

    /* First chat detected: drops 10.0 HP (100 tenths) */
    assert(mc_study_chat_detected(&s, 1000));
    assert(s.hp == 90 && s.hp_tenths == 900 && s.penalties == 1);

    /* 1-second debounce: within 1000ms should be ignored */
    assert(!mc_study_chat_detected(&s, 1500));
    assert(!mc_study_chat_detected(&s, 1999));
    assert(s.hp == 90 && s.hp_tenths == 900 && s.penalties == 1);

    /* After 1000ms, drops another 10.0 HP */
    assert(mc_study_chat_detected(&s, 2000));
    assert(s.hp == 80 && s.hp_tenths == 800 && s.penalties == 2);
}

static void test_pause_bleeding(void)
{
    mc_study s;
    mc_study_reset(&s);
    mc_study_set_active(&s, true);

    /* Pause the session at t = 10000 */
    mc_study_set_paused(&s, true, 10000);
    assert(!s.active && s.paused_bleeding);

    /* Less than 1s elapsed: no tick */
    assert(!mc_study_pause_tick(&s, 10500));
    assert(s.hp_tenths == 1000);

    /* Exactly 1s elapsed: bleeds 0.2 HP (2 tenths) */
    assert(mc_study_pause_tick(&s, 11000));
    assert(s.hp_tenths == 998 && s.hp == 99);

    /* 5s leap: bleeds 5 * 2 = 10 tenths */
    assert(mc_study_pause_tick(&s, 16000));
    assert(s.hp_tenths == 988 && s.hp == 98);

    /* Resume clears bleeding */
    mc_study_set_paused(&s, false, 17000);
    assert(!s.paused_bleeding);
    assert(!mc_study_pause_tick(&s, 20000));
    assert(s.hp_tenths == 988);
}

static void test_posture_tracking_and_penalties(void)
{
    mc_study s;
    mc_study_reset(&s);
    mc_study_set_active(&s, true);

    /* Abnormal posture accumulating up to 15s */
    assert(!mc_study_pose(&s, MC_TILTED, 0));
    assert(!mc_study_pose(&s, MC_TILTED, 10000));
    assert(!mc_study_pose(&s, MC_TILTED, 14999));

    /* At 15s (15000ms): triggers reminder and deducts 5.0 HP (50 tenths) */
    assert(mc_study_pose(&s, MC_TILTED, 15000));
    assert(s.reminders == 1 && s.penalties == 1);
    assert(s.hp == 95 && s.hp_tenths == 950);

<<<<<<< Updated upstream
    /* Sparse results must not manufacture 15 seconds of visual evidence. */
    mc_study_reset(&s);
    mc_study_set_active(&s, true);
    assert(!mc_study_pose(&s, MC_TILTED, 0));
    assert(!mc_study_pose(&s, MC_TILTED, 8000));
    assert(!mc_study_pose(&s, MC_TILTED, 16000));
    assert(s.abnormal_ms == 4000 && s.reminders == 0);
    assert(!mc_study_pose(&s, MC_NORMAL, 18000));
    assert(s.abnormal_ms == 0);
    assert(!mc_study_pose(&s, MC_TILTED, 20000));
    assert(!mc_study_pose(&s, MC_UNKNOWN, 22000));
    assert(!mc_study_pose(&s, MC_TILTED, 24000));
    assert(s.abnormal_ms == 0);
    assert(!mc_study_pose(&s, MC_TILTED, 33000));
    assert(s.abnormal_ms == 0); /* gap >8s starts a new sequence */
    assert(!mc_study_pose(&s, MC_TILTED, 32000));
    assert(s.abnormal_ms == 0); /* clock rollback */

=======
    /* Notice cooldown: 60s cooldown before next reminder */
    assert(!mc_study_pose(&s, MC_TILTED, 25000));
    assert(!mc_study_pose(&s, MC_TILTED, 74999));
    assert(s.reminders == 1);

    /* Normal posture breaks continuity */
    assert(!mc_study_pose(&s, MC_NORMAL, 75000));
    assert(!s.has_pose && s.abnormal_ms == 0);

    /* Next abnormal needs another full 15s to trigger */
    assert(!mc_study_pose(&s, MC_HEAD_LOWERED, 76000));
    assert(!mc_study_pose(&s, MC_HEAD_LOWERED, 85000));
    assert(mc_study_pose(&s, MC_HEAD_LOWERED, 91000));
    assert(s.reminders == 2 && s.penalties == 2);
    assert(s.hp == 90 && s.hp_tenths == 900);
}

static void test_exhaustion(void)
{
    mc_study s;
>>>>>>> Stashed changes
    mc_study_reset(&s);
    mc_study_set_active(&s, true);

    /* Deduct 10 times via chat */
    for (uint64_t i = 0; i < 9; ++i) {
        assert(mc_study_chat_detected(&s, (i + 1) * 2000));
    }
    assert(s.hp == 10 && s.hp_tenths == 100 && !s.exhausted);

    /* 10th penalty brings HP to 0 -> exhausted */
    assert(mc_study_chat_detected(&s, 20000));
    assert(s.hp == 0 && s.hp_tenths == 0 && s.exhausted && !s.active);

    /* Once exhausted, further calls cannot deduct or activate until reset */
    mc_study_set_active(&s, true);
    assert(!s.active);
    assert(!mc_study_chat_detected(&s, 30000));
    assert(!mc_study_pose(&s, MC_TILTED, 45000));
}

int main(void)
{
    test_initial_and_chat_penalties();
    test_pause_bleeding();
    test_posture_tracking_and_penalties();
    test_exhaustion();
    puts("All mc_study assertion tests passed successfully!");
    return 0;
}

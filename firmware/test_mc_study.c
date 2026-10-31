#include "mc_study.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    mc_study s;
    mc_study_reset(&s);
    assert(s.hp == 100 && !s.active);
    assert(!mc_study_distraction(&s, true, 0));
    mc_study_set_active(&s, true);
    assert(!mc_study_distraction(&s, false, 0));
    assert(mc_study_distraction(&s, true, 0));
    assert(!mc_study_distraction(&s, true, 59999));
    assert(mc_study_distraction(&s, true, 60000));
    assert(!mc_study_distraction(&s, true, 100)); /* clock rollback */
    assert(s.hp == 80 && s.penalties == 2);

    mc_study_reset(&s);
    mc_study_set_active(&s, true);
    for (uint64_t t = 0; t < 15000; t += 1000)
        assert(!mc_study_pose(&s, MC_TILTED, t));
    assert(mc_study_pose(&s, MC_TILTED, 15000));
    for (uint64_t t = 16000; t < 75000; t += 1000)
        assert(!mc_study_pose(&s, MC_TILTED, t));
    assert(mc_study_pose(&s, MC_TILTED, 75000));
    assert(s.hp == 100 && s.reminders == 2);
    mc_study_set_active(&s, false);
    assert(!mc_study_pose(&s, MC_TILTED, 76000));
    mc_study_set_active(&s, true);
    assert(!mc_study_pose(&s, MC_TILTED, 77000));
    assert(!mc_study_pose(&s, MC_TILTED, 90000)); /* stale gap */
    assert(s.abnormal_ms == 0);
    assert(!mc_study_pose(&s, MC_UNKNOWN, 91000));
    assert(!s.has_pose);

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

    mc_study_reset(&s);
    mc_study_set_active(&s, true);
    for (uint64_t i = 0; i < 10; ++i)
        assert(mc_study_distraction(&s, true, i * 60000));
    assert(s.hp == 0 && s.exhausted && !s.active);
    mc_study_set_active(&s, true);
    assert(!s.active); /* fresh session required */
    assert(!mc_study_distraction(&s, true, 600000));
    puts("mc_study assertions passed");
    return 0;
}

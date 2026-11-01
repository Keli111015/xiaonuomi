"""Run real servo C code with mocked PWM/tasks; no hardware movement.
Usage: python tools/tests/test_servo_host.py [--cc <gcc>]
Or: python -m unittest discover -s tools/tests -p "test_*.py"
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile
import unittest


def run_all_tests(cc='gcc'):
    root = pathlib.Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='servo-test-') as tmp:
        p = pathlib.Path(tmp)
        (p / 'qm.h').write_text(r"""
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int qm_err_t; typedef int qm_mutex_t; typedef int qm_task_t;
#define QM_EOK 0
#define QM_EBUSY 16
#define QM_ENOMEM 12
#define QM_EIO 5
#define QM_WAIT_FOREVER -1
#define CONFIG_QM_CLI_SUPPORT 0
int qm_mutex_new(qm_mutex_t*); int qm_mutex_lock(qm_mutex_t*,int); int qm_mutex_unlock(qm_mutex_t*);
int qm_task_new(qm_task_t*,const char*,void(*)(void*),void*,int,int);
void qm_msleep(int); void qm_task_exit(void*);
""")
        (p / 'qm_log.h').write_text('#define QM_LOGI(...) ((void)0)\n#define QM_LOGW(...) ((void)0)\n#define QM_LOGE(...) ((void)0)\n')
        (p / 'qm_cli.h').write_text('')
        (p / 'qm_pwm.h').write_text(r"""
#pragma once
#include "qm.h"
typedef struct {unsigned freq,pulse;} qm_pwm_config_t;
typedef struct {int port; struct {int pwm_pin;} pin; qm_pwm_config_t config;} qm_pwm_dev_t;
int qm_pwm_init(qm_pwm_dev_t*); int qm_pwm_stop(qm_pwm_dev_t*);
int qm_pwm_para_change(qm_pwm_dev_t*,qm_pwm_config_t);
""")
        for name in ('bsp_servo.c', 'bsp_servo.h'):
            (p / name).write_bytes((root / 'sdk/apps/main/peripheral' / name).read_bytes())
        source = 'bsp_servo.c'
        (p / 'test.c').write_text('#include <assert.h>\n#include <stdio.h>\n#include "' + source + '"\n' + r"""
static int budget, writes, fail_pwm, stops, tasks, seen[5], pos;
int qm_mutex_new(qm_mutex_t*x){return 0;}
int qm_mutex_lock(qm_mutex_t*x,int n){return 0;}
int qm_mutex_unlock(qm_mutex_t*x){return 0;}
int qm_task_new(qm_task_t*t,const char*n,void(*f)(void*),void*a,int s,int pr){tasks++;return 0;}
int qm_pwm_init(qm_pwm_dev_t*p){return 0;}
int qm_pwm_stop(qm_pwm_dev_t*p){stops++;return 0;}
int qm_pwm_para_change(qm_pwm_dev_t*p,qm_pwm_config_t c){writes++;return fail_pwm?-QM_EIO:0;}
void qm_task_exit(void*a){}
void qm_msleep(int ms){
 assert(ms==40);
 if(s_output_ticks) {
  assert(s_target_angle>=60 && s_target_angle<=120);
  if(pos<5 && s_pulse_us==servo_angle_to_pulse(s_reminder_angles[pos])) seen[pos++]=1;
 }
 if(--budget==0)s_move_run=0;
}
static void run(int n){budget=n;s_move_run=1;servo_move_task(NULL);}
int main(void){
 const int16_t expected[]={60,120,60,120,90};
 for(int i=0;i<5;i++)assert(s_reminder_angles[i]==expected[i]);
 assert(bsp_servo_init()==0 && writes==0 && tasks==1);
 assert(bsp_servo_init()==0 && tasks==1);
 s_init_busy=1;assert(bsp_servo_init()==-QM_EBUSY);s_init_busy=0;
 assert(bsp_servo_focus_reminder()==0);
 assert(bsp_servo_focus_reminder()==-QM_EBUSY);
 run(150);assert(pos==5 && s_pulse_us==1500 && s_output_ticks==0 && s_reminder_ticks==0);
 assert(bsp_servo_focus_reminder()==0);run(4);bsp_servo_deinit();
 int before=writes;run(90);assert(writes==before && s_reminder_ticks==0);
 assert(bsp_servo_focus_reminder()==0);run(4);bsp_servo_set_angle(90);run(70);
 assert(s_pulse_us==1500 && s_reminder_ticks==0 && s_output_ticks==0);
 assert(bsp_servo_focus_reminder()==0);fail_pwm=1;run(1);
 assert(s_reminder_ticks==0 && s_output_ticks==0);fail_pwm=0;
 assert(bsp_servo_focus_reminder()==0);run(8);assert(s_pulse_us==servo_angle_to_pulse(60));
 fail_pwm=1;run(1);assert(s_output_ticks==0 && s_reminder_ticks==0);fail_pwm=0;
 bsp_servo_set_angle(60);run(70);assert(s_pulse_us==servo_angle_to_pulse(60) && !s_output_ticks);
 bsp_servo_set_angle(120);run(70);assert(s_pulse_us==servo_angle_to_pulse(120) && !s_output_ticks);
 before=writes;bsp_servo_set_angle(59);bsp_servo_set_angle(121);bsp_servo_set_angle(180);run(10);assert(writes==before);
 puts("PASS: idle boot, init guard, full sequence, busy rejection, stop, manual override, ramp/hold faults, angle bound");
}
""")
        exe = p / 'test.exe'
        subprocess.run([cc, '-std=c11', '-I', str(p), str(p / 'test.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

        # Exercise the real runtime event handler independently of FreeRTOS.
        (p / 'qm_errno.h').write_text('#include "qm.h"\n')
        (p / 'qm_types.h').write_text('#include <stdint.h>\ntypedef int qm_bool;\n')
        (p / 'qm_pomodoro.h').write_bytes((root / 'sdk/qumios-dev/components/qm_pomodoro/include/qm_pomodoro.h').read_bytes())
        runtime = (root / 'sdk/apps/main/lj_components/morphcube/morphcube_study_runtime.c').read_text(encoding='utf-8')
        handler = runtime[runtime.index('static bool s_end_reminder_sent;'):runtime.index('static void finish_exhausted_round(')]
        exhaustion = runtime[runtime.index('static void finish_exhausted_round('):runtime.index('static void morphcube_study_task(')]
        (p / 'event.c').write_text(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "qm.h"
#include "qm_log.h"
#include "qm_pomodoro.h"
typedef struct { bool exhausted; } mc_study;
static int reminders;
static uint64_t monotonic_ms(void) { return 0; }
void mc_study_reset(mc_study *s){}
void mc_study_set_active(mc_study*s,bool active){}
void mc_study_set_paused(mc_study*s,bool paused,uint64_t now_ms){}
int bsp_servo_focus_reminder(void){reminders++;return 0;}
static qm_pomodoro_t s_timer;
static int stop_result, forwards, cloud_stops, deaths;
int qm_pomodoro_stop(qm_pomodoro_t timer){return stop_result;}
int board_bridge_forward(const char *command){assert(strcmp(command,"stop")==0);forwards++;return 0;}
int ljyd_method_up(const char *method,const char *sub,const void *params){assert(strcmp(method,"pomodoro")==0 && strcmp(sub,"stop")==0);cloud_stops++;return 0;}
int death_sound_start(void){deaths++;return 0;}
""" + handler + exhaustion + r"""
int main(void){
 mc_study study={0};qm_pomodoro_status_t status={0};
 for(int event=QM_POMODORO_EVENT_STATE_CHANGED;event<=QM_POMODORO_EVENT_FINISHED;event++){
  for(int phase=QM_POMODORO_STATE_IDLE;phase<=QM_POMODORO_STATE_FINISHED;phase++){
   for(int skipped=0;skipped<=1;skipped++){
    qm_pomodoro_event_data_t e={0};e.event=event;e.previous_state=phase;e.phase_skipped=skipped;
    s_end_reminder_sent=false;
    int before=reminders;handle_pomodoro(&study,&status,&e);
    assert(reminders-before==(phase==QM_POMODORO_STATE_FOCUS && ((event==QM_POMODORO_EVENT_PHASE_DONE && !skipped) || event==QM_POMODORO_EVENT_STOPPED)));
   }
  }
 }
 study.exhausted=true;s_end_reminder_sent=false;
 qm_pomodoro_event_data_t e={0};e.event=QM_POMODORO_EVENT_STOPPED;e.previous_state=QM_POMODORO_STATE_FOCUS;
 int before=reminders;handle_pomodoro(&study,&status,&e);assert(reminders==before+1);
 handle_pomodoro(&study,&status,&e);assert(reminders==before+1); /* duplicate stop */
 s_end_reminder_sent=false;e.event=QM_POMODORO_EVENT_PHASE_DONE;
 before=reminders;handle_pomodoro(&study,&status,&e);assert(reminders==before+1);
 e.event=QM_POMODORO_EVENT_FINISHED;handle_pomodoro(&study,&status,&e);assert(reminders==before+1);
 e.event=QM_POMODORO_EVENT_STOPPED;handle_pomodoro(&study,&status,&e);assert(reminders==before+1);
 e.event=QM_POMODORO_EVENT_STATE_CHANGED;e.previous_state=QM_POMODORO_STATE_IDLE;e.status.state=QM_POMODORO_STATE_FOCUS;
 handle_pomodoro(&study,&status,&e);assert(!s_end_reminder_sent);
 e.event=QM_POMODORO_EVENT_STOPPED;e.previous_state=QM_POMODORO_STATE_FOCUS;
 handle_pomodoro(&study,&status,&e);assert(reminders==before+2); /* next focus may remind */
 stop_result=0;finish_exhausted_round();assert(forwards==1 && cloud_stops==1 && deaths==1);
 stop_result=-5;finish_exhausted_round();assert(forwards==1 && cloud_stops==1 && deaths==2);
 puts("PASS: 70 event combinations, zero-HP stop, duplicate suppression, next-focus reset, exhaustion stop forwarding");
}
""", encoding='utf-8')
        exe = p / 'event.exe'
        subprocess.run([cc, '-std=c11', '-I', str(p), str(p / 'event.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class TestServoHost(unittest.TestCase):
    def test_servo_host(self):
        run_all_tests('gcc')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args, unknown = parser.parse_known_args()
    run_all_tests(args.cc)

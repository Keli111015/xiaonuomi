"""Exercise actual ESP-NOW worker using deterministic mocked transport/tasks."""
from pathlib import Path
import tempfile,subprocess,argparse
ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');a=ap.parse_args();r=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'freertos').mkdir()
 common=r"""
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_INVALID_ARG 4
#define WIFI_IF_STA 0
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
typedef void* QueueHandle_t;
typedef struct {uint8_t *src_addr,*des_addr;} esp_now_recv_info_t;
typedef struct {uint8_t peer_addr[6],lmk[16],channel;int ifidx;bool encrypt;} esp_now_peer_info_t;
int xQueueSend(void*,const void*,int);int xQueueReceive(void*,void*,int);void *xQueueCreate(int,int);void vQueueDelete(void*);
int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*);
int esp_now_init(void);int esp_now_set_pmk(const uint8_t*);int esp_now_add_peer(const esp_now_peer_info_t*);
int esp_now_register_recv_cb(void(*)(const esp_now_recv_info_t*,const uint8_t*,int));int esp_now_deinit(void);
int esp_now_send(const uint8_t*,const uint8_t*,size_t);int esp_wifi_get_mac(int,uint8_t*);
uint32_t esp_random(void);int64_t esp_timer_get_time(void);const char *esp_err_to_name(int);
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_EARLY_LOGW(...) ((void)0)
"""
 (p/'common.h').write_text(common)
 for n in ['qm_config.h','esp_err.h','esp_now.h','esp_wifi.h','esp_random.h','esp_timer.h','esp_log.h','freertos/FreeRTOS.h','freertos/queue.h','freertos/task.h']:(p/n).write_text('#include "common.h"\n')
 for n in ['board_bridge.c','board_bridge.h']:(p/n).write_bytes((r/'sdk/apps/main/lj_components/pomodoro'/n).read_bytes())
 (p/'board_bridge_local.h').write_text('#define BOARD_BRIDGE_VOICE_MAC {1,2,3,4,5,6}\n#define BOARD_BRIDGE_ACTION_MAC {6,5,4,3,2,1}\n#define BOARD_BRIDGE_PMK {1}\n#define BOARD_BRIDGE_LMK {2}\n')
 (p/'pomodoro.h').write_text('int pomodoro_execute_command(const char*);\n')
 (p/'test.c').write_text(r"""
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "board_bridge.c"
static jmp_buf done;static event_t input[5];static int count,index_,ticks,sends,executions;static packet_t sent[16];
int xQueueReceive(void*q,void*e,int t){if(ticks++>12)longjmp(done,1);if(index_<count){memcpy(e,&input[index_++],sizeof(event_t));return 1;}return 0;}
int xQueueSend(void*q,const void*e,int t){return 1;}void*xQueueCreate(int n,int s){return (void*)1;}void vQueueDelete(void*q){}
int xTaskCreate(void(*f)(void*),const char*n,int s,void*a,int pr,void*h){return 1;}
int esp_now_send(const uint8_t*m,const uint8_t*b,size_t n){assert(n==20);sent[sends++]=*(const packet_t*)b;return 0;}
int esp_now_init(void){return 0;}int esp_now_set_pmk(const uint8_t*p){return 0;}int esp_now_add_peer(const esp_now_peer_info_t*p){assert(p->encrypt);return 0;}
int esp_now_register_recv_cb(void(*f)(const esp_now_recv_info_t*,const uint8_t*,int)){return 0;}int esp_now_deinit(void){return 0;}
int esp_wifi_get_mac(int i,uint8_t*m){memcpy(m,voice_mac,6);return 0;}uint32_t esp_random(void){return 123;}int64_t esp_timer_get_time(void){return (int64_t)ticks*2000000;}
const char*esp_err_to_name(int e){return "mock";}int pomodoro_execute_command(const char*c){assert(!strcmp(c,"stop"));executions++;return 0;}
static void run(void){if(!setjmp(done))worker(NULL);}
static void reset(void){ticks=index_=sends=executions=0;memset(input,0,sizeof(input));}
int main(void){
 assert(board_bridge_init()==0 && voice && ready);
 assert(board_bridge_forward(NULL)==ESP_ERR_INVALID_ARG);
 reset();voice=false;assert(board_bridge_forward("stop")==ESP_ERR_INVALID_STATE);count=3;
 for(int i=0;i<3;i++){input[i].packet=(packet_t){MAGIC,7,1,1,1,3,0,0};}
 run();assert(executions==1 && sends==3);for(int i=0;i<3;i++)assert(sent[i].type==2 && sent[i].result==0);
 reset();voice=true;count=1;input[0].local=true;input[0].packet.command=3;run();assert(sends==3 && executions==0);assert(sent[0].seq==sent[2].seq);
 reset();voice=true;count=2;input[0].local=true;input[0].packet.command=0;input[1].local=true;input[1].packet.command=3;run();assert(sent[0].command==0 && sent[1].command==3 && sent[1].seq>sent[0].seq);
 reset();voice=true;count=2;input[0].local=true;input[0].packet.command=3;
 input[1].packet=(packet_t){MAGIC,123,1,1,2,3,0,0};run();assert(sends==1);
 reset();voice=false;count=2;input[0].packet=(packet_t){MAGIC,7,3,1,1,3,0,0};input[1].packet=(packet_t){MAGIC,7,2,1,1,3,0,0};run();assert(executions==1 && sent[1].result==ESP_ERR_INVALID_STATE);
 puts("PASS: encrypted peer, duplicate execution suppression, bounded retries, new intent ordering, stale sequence rejection");
}
""")
 subprocess.run([a.cc,'-std=c11','-I',str(p),str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)

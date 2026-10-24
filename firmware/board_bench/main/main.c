/* Bare-board bench only: no cloud, camera, motors, audio or Wi-Fi. */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "mc_study.h"

static mc_study study;
static uint64_t remaining_ms = 1500000, previous_ms;
static const char *phase = "idle";

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static void status(const char *result)
{
    printf("{\"result\":\"%s\",\"mode\":\"board_bench\",\"phase\":\"%s\","
           "\"remaining_ms\":%" PRIu64 ",\"hp\":%u,\"distractions\":%" PRIu32
           ",\"camera\":false,\"cloud\":false}\n",
           result, phase, remaining_ms, (unsigned)study.hp, study.penalties);
    fflush(stdout);
}

static void update_clock(uint64_t now)
{
    uint64_t dt = now - previous_ms;
    previous_ms = now;
    if (!study.active) return;
    if (dt >= remaining_ms) {
        remaining_ms = 0;
        phase = "completed";
        mc_study_set_active(&study, false);
        status("completed");
    } else remaining_ms -= dt;
}

static void command(const char *line, uint64_t now)
{
    const char *result = "ok";
    if (!strcmp(line, "status")) { /* read only */ }
    else if (!strcmp(line, "start")) {
        if (strcmp(phase, "idle")) result = "invalid_state";
        else { phase = "focus"; mc_study_set_active(&study, true); }
    } else if (!strcmp(line, "pause")) {
        if (study.active) { phase = "paused"; mc_study_set_active(&study, false); }
        else result = "invalid_state";
    } else if (!strcmp(line, "resume")) {
        if (strcmp(phase, "paused") || study.exhausted) result = "invalid_state";
        else { phase = "focus"; mc_study_set_active(&study, true); }
    } else if (!strcmp(line, "distracted")) {
        if (!mc_study_distraction(&study, true, now)) result = "ignored";
        if (study.exhausted) phase = "rest_required";
    } else if (!strcmp(line, "stop")) {
        mc_study_set_active(&study, false);
        phase = "stopped";
    } else if (!strcmp(line, "new")) {
        /* Explicit new round only; never reset on duplicated start. */
        if (study.active) result = "stop_first";
        else { mc_study_reset(&study); remaining_ms = 1500000; phase = "idle"; }
    } else result = "unknown_command";
    status(result);
}

void app_main(void)
{
    /* UART0 uses console board defaults. No external GPIO outputs configured. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0));
    mc_study_reset(&study);
    previous_ms = now_ms();
    status("ready");
    char line[64];
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        uint8_t byte;
        int count = uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(20));
        uint64_t now = now_ms();
        update_clock(now);
        if (count != 1) continue;
        if (byte == '\r' || byte == '\n') {
            if (overflow) status("line_too_long");
            else if (used) { line[used] = '\0'; command(line, now); }
            used = 0;
            overflow = false;
        } else if (!overflow) {
            if (byte < 32 || byte > 126 || used >= sizeof(line) - 1) overflow = true;
            else line[used++] = (char)byte;
        }
    }
}

#include <cstdio>
#include <csignal>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <unistd.h>

#include "telemetry.h"
#include "ui_engine.h"

extern "C" {
    #include "DEV_Config.h"
    #include "EPD_2in13_V4.h"
}

static volatile bool running = true;
static volatile sig_atomic_t exit_signal = 0;

static void on_signal(int sig) {
    exit_signal = sig;
    running = false;
}

int main() {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    printf("[ SENTINEL ] Boot sequence starting\n");

    if (DEV_Module_Init() != 0) {
        fprintf(stderr, "[ ERR ] Hardware init failed\n");
        return 1;
    }
    printf("[ OK  ] Hardware init\n");

    EPD_2in13_V4_Init();
    EPD_2in13_V4_Clear();
    printf("[ OK  ] Display ready\n");

    UIEngine ui;
    if (!ui.init()) {
        fprintf(stderr, "[ ERR ] UIEngine init failed (malloc)\n");
        return 1;
    }
    printf("[ OK  ] UI engine init\n");

    unsigned long frame_no = 0;
    time_t last_render_second = -1;
    struct timespec last_tick {};
    clock_gettime(CLOCK_MONOTONIC, &last_tick);
    while (running) {
        const time_t now = time(nullptr);
        if (now == last_render_second) {
            usleep(20000);
            continue;
        }

        last_render_second = now;
        ++frame_no;
        SystemMetrics metrics;
        collect_metrics(metrics);
        struct timespec tick {};
        clock_gettime(CLOCK_MONOTONIC, &tick);
        const long elapsed_ms =
            static_cast<long>((tick.tv_sec - last_tick.tv_sec) * 1000 +
                              (tick.tv_nsec - last_tick.tv_nsec) / 1000000);
        metrics.heartbeat_jitter_ms = std::abs(static_cast<int>(elapsed_ms - 1000));
        metrics.heartbeat_missed_ticks = (elapsed_ms > 2000) ? static_cast<int>((elapsed_ms / 1000) - 1) : 0;
        last_tick = tick;
        ui.render(metrics);
        printf("[METRICS] CPU=%5.1f%% TEMP=%4.1fC MEM=%3d%% IP=%s UPTIME=%lds\n",
               metrics.cpu_pct,
               metrics.cpu_temp_c,
               metrics.mem_pct,
               metrics.ip.c_str(),
               metrics.uptime_s);
        if (frame_no % 30 == 0) {
            printf("[NETTEST] SSID=%s RX=%6.0fKB/s TX=%6.0fKB/s PLOSS=%.1f%%\n",
                   metrics.ssid.c_str(),
                   metrics.net_rx_kbps,
                   metrics.net_tx_kbps,
                   metrics.packet_loss_pct);
        }

    }

    SystemMetrics final_metrics;
    collect_metrics(final_metrics);
    const char *reason = (exit_signal == SIGINT) ? "SIGINT" :
                         (exit_signal == SIGTERM) ? "SIGTERM" : "NORMAL";
    ui.render_shutdown(final_metrics, reason);
    printf("[ -- ] Shutting down (reason=%s)\n", reason);
    ui.sleep();
    DEV_Delay_ms(2000);
    DEV_Module_Exit();
    printf("[ OK ] Goodbye\n");
    return 0;
}
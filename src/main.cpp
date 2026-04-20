#include <cstdio>
#include <csignal>
#include <unistd.h>

#include "telemetry.h"
#include "ui_engine.h"

extern "C" {
    #include "DEV_Config.h"
    #include "EPD_2in13_V4.h"
}

// Refresh interval in seconds. e-paper has a limited number of full
// refreshes, so 30s is a reasonable balance for a status terminal.
static constexpr int REFRESH_INTERVAL_S = 30;

static volatile bool running = true;

static void on_signal(int) {
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

    while (running) {
        SystemMetrics metrics;
        collect_metrics(metrics);
        ui.render(metrics);
        printf("[ OK  ] Frame rendered — CPU %.1f%% TEMP %.1fC MEM %d%%\n",
               metrics.cpu_pct, metrics.cpu_temp_c, metrics.mem_pct);

        // Sleep in 1s increments so SIGTERM is handled promptly
        for (int i = 0; i < REFRESH_INTERVAL_S && running; i++)
            sleep(1);
    }

    printf("[ -- ] Shutting down\n");
    ui.sleep();
    DEV_Delay_ms(2000);
    DEV_Module_Exit();
    printf("[ OK ] Goodbye\n");
    return 0;
}
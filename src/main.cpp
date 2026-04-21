#include <cstdio>
#include <csignal>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "external_log_watcher.h"
#include "logger.h"
#include "telemetry.h"
#include "ui_engine.h"
#include "web_server.h"

#ifdef HAVE_WAVESHARE
extern "C" {
    #include "DEV_Config.h"
    #include "EPD_2in13_V4.h"
}
#endif

static volatile bool running = true;
static volatile sig_atomic_t exit_signal = 0;
static volatile sig_atomic_t reload_logs = 0;

static void on_signal(int sig) {
    if (sig == SIGHUP) {
        reload_logs = 1;
        return;
    }
    exit_signal = sig;
    running = false;
}

static int read_env_int(const char *name, int default_value, int min_value) {
    const char *v = std::getenv(name);
    if (!v || !*v) return default_value;
    int parsed = std::atoi(v);
    if (parsed < min_value) return min_value;
    return parsed;
}

int main() {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP,  on_signal);

    const char *log_path_env = std::getenv("SENTINEL_LOG_FILE");
    std::string log_path = (log_path_env && *log_path_env)
                           ? std::string(log_path_env)
                           : std::string("/var/log/sentinel.log");
    Logger logger(log_path);
    if (!logger.init()) {
        fprintf(stderr, "[ ERR ] Logger init failed\n");
        return 1;
    }
    logger.info("Sentinel boot sequence starting");
    logger.info(std::string("Local log path: ") + logger.path());

    ExternalLogWatcher watcher;
    const char *watch_env = std::getenv("SENTINEL_WATCH");
    if (watch_env && *watch_env) {
        watcher.configure_from_spec(watch_env);
        if (!watcher.empty()) {
            logger.info(std::string("Watching external logs: ") + watcher.describe_sources());
        }
    }

    bool display_enabled = true;
    const char *disable_display_env = std::getenv("SENTINEL_DISABLE_DISPLAY");
    if (disable_display_env && std::string(disable_display_env) == "1") {
        display_enabled = false;
    }

    const int stat_log_interval_s = read_env_int("SENTINEL_LOG_INTERVAL_S", 2, 1);
    const int watch_max_events = read_env_int("SENTINEL_WATCH_MAX_EVENTS", 32, 1);
    const int web_enabled = read_env_int("SENTINEL_WEB_ENABLE", 0, 0);
    const int web_port = read_env_int("SENTINEL_WEB_PORT", 9090, 1);
    logger.info(std::string("STAT log interval: ") + std::to_string(stat_log_interval_s) + "s");
    logger.info(std::string("External watch batch size: ") + std::to_string(watch_max_events));

    WebServer web;
    if (web_enabled == 1) {
        const char *bind_env = std::getenv("SENTINEL_WEB_BIND");
        const char *token_env = std::getenv("SENTINEL_WEB_TOKEN");

        WebConfig cfg;
        cfg.enabled = true;
        cfg.bind_addr = (bind_env && *bind_env) ? std::string(bind_env) : std::string("127.0.0.1");
        cfg.port = web_port;
        cfg.token = (token_env && *token_env) ? std::string(token_env) : std::string();

        if (web.start(cfg)) {
            logger.info(std::string("Web view enabled at ") + cfg.bind_addr + ":" + std::to_string(cfg.port));
            if (!cfg.token.empty()) {
                logger.info("Web API token auth enabled (X-Sentinel-Token)");
            }
        } else {
            logger.error("Web view failed to start; continuing without web");
        }
    }

    bool hw_ready = false;
    bool display_ready = false;

    UIEngine ui;
    if (display_enabled) {
#ifdef HAVE_WAVESHARE
        if (DEV_Module_Init() == 0) {
            hw_ready = true;
            EPD_2in13_V4_Init();
            EPD_2in13_V4_Clear();
            display_ready = ui.init();
            if (!display_ready) {
                logger.warn("Display hardware found but UI buffer init failed; running headless");
            } else {
                logger.info("Display ready");
            }
        } else {
            logger.warn("Display init failed; running headless");
        }
#else
        logger.warn("Display requested but Waveshare vendor sources are not built in; running headless");
#endif
    } else {
        logger.info("Display disabled by SENTINEL_DISABLE_DISPLAY=1");
    }

    unsigned long frame_no = 0;
    time_t last_render_second = -1;
    struct timespec last_tick {};
    clock_gettime(CLOCK_MONOTONIC, &last_tick);
    std::string latest_external_event;
    bool last_warn_state = false;
    std::string last_warn_code;

    while (running) {
        if (reload_logs) {
            reload_logs = 0;
            if (logger.reopen()) {
                logger.info("Log file reopened (SIGHUP)");
            } else {
                logger.error("Log reopen failed after SIGHUP");
            }
        }

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

        auto ext_events = watcher.poll(static_cast<std::size_t>(watch_max_events));
        for (const auto &ev : ext_events) {
            latest_external_event = ev.source + ": " + ev.line;
            std::string line = std::string("EXT[") + ev.source + "] " + ev.line;
            logger.info(line);
            web.push_event(line);
        }
        metrics.event_line = latest_external_event;
        web.update_metrics(metrics);

        if (display_ready) {
            ui.render(metrics);
        }

        const bool warn_cpu = metrics.cpu_pct > 85.0f;
        const bool warn_temp = metrics.cpu_temp_c > 70.0f;
        const bool warn_mem = metrics.mem_pct > 85;
        const bool warn_net = (metrics.ip == "NO_LINK");
        const bool warn_state = warn_cpu || warn_temp || warn_mem || warn_net;

        char summary[256];
        snprintf(summary, sizeof(summary),
                 "STAT %s CPU:%4.1f%% TMP:%4.1fC MEM:%3d%% NET:%s",
                 metrics.time_str,
                 metrics.cpu_pct,
                 metrics.cpu_temp_c,
                 metrics.mem_pct,
                 warn_net ? "DOWN" : "UP");
        if (frame_no % static_cast<unsigned long>(stat_log_interval_s) == 0) {
            logger.info(summary);
            web.push_event(summary);
        }

        if (frame_no % 30 == 0) {
            char netline[192];
            if (metrics.packet_loss_pct < 0.0f) {
                snprintf(netline, sizeof(netline),
                         "NET  SSID:%s LOSS:N/A IF:%s IP:%s",
                         metrics.ssid.c_str(),
                         metrics.iface.c_str(),
                         metrics.ip.c_str());
            } else {
                snprintf(netline, sizeof(netline),
                         "NET  SSID:%s LOSS:%0.1f%% IF:%s IP:%s",
                         metrics.ssid.c_str(),
                         metrics.packet_loss_pct,
                         metrics.iface.c_str(),
                         metrics.ip.c_str());
            }
            logger.info(netline);
            web.push_event(netline);
        }

        if (warn_state) {
            std::string warn_code = warn_temp ? "THERMAL_LIMIT" :
                                    warn_cpu ? "CPU_LOAD_HIGH" :
                                    warn_mem ? "MEM_PRESSURE" :
                                               "NETWORK_DOWN";
            if (!last_warn_state || warn_code != last_warn_code) {
                std::string line = std::string("ALERT ") + warn_code;
                logger.warn(line);
                web.push_event(line);
            }
            last_warn_code = warn_code;
        } else if (last_warn_state) {
            logger.info("ALERT CLEARED");
            web.push_event("ALERT CLEARED");
            last_warn_code.clear();
        }
        last_warn_state = warn_state;

    }

    SystemMetrics final_metrics;
    collect_metrics(final_metrics);
    final_metrics.event_line = latest_external_event;
    const char *reason = (exit_signal == SIGINT) ? "SIGINT" :
                         (exit_signal == SIGTERM) ? "SIGTERM" : "NORMAL";

    if (display_ready) {
        ui.render_shutdown(final_metrics, reason);
        ui.sleep();
    }
    if (hw_ready) {
#ifdef HAVE_WAVESHARE
        DEV_Delay_ms(2000);
        DEV_Module_Exit();
#endif
    }

    web.stop();

    logger.info(std::string("Shutting down (reason=") + reason + ")");
    logger.info("Goodbye");
    return 0;
}
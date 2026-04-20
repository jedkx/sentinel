#include "ui_engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
    #include "DEV_Config.h"
    #include "GUI_Paint.h"
}

// ---------------------------------------------------------------------------
// Layout constants  (all Y coords relative to horizontal canvas 250x122)
// ---------------------------------------------------------------------------

// Row boundaries
static constexpr int Y_HEADER_TOP  =   0;
static constexpr int Y_HEADER_BOT  =  11;
static constexpr int Y_H0          =  12; // solid line after header

static constexpr int Y_TIME_TXT    =  15;
static constexpr int Y_H1          =  24; // dotted

static constexpr int Y_CPU_TXT     =  27;
static constexpr int Y_H2          =  36; // dotted

static constexpr int Y_MEM_TXT     =  39;
static constexpr int Y_MEM_BAR     =  49; // bar top
static constexpr int Y_MEM_BAR_H   =   5; // bar height
static constexpr int Y_H3          =  57; // solid

static constexpr int Y_UPIP_TXT    =  60;
static constexpr int Y_H4          =  70; // dotted

static constexpr int Y_LOG_L0      =  72;
static constexpr int Y_LOG_L1      =  80;
static constexpr int Y_LOG_L2      =  88;
static constexpr int Y_LOG_L3      =  96;
static constexpr int Y_LOG_L4      = 104;
static constexpr int Y_LOG_L5      = 112;

// Grid: vertical separator between left/right columns
static constexpr int X_SPLIT       = 124;

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

UIEngine::UIEngine() : buffer_(nullptr), frame_count_(0), terminal_seeded_(false) {
    terminal_lines_.fill("> WAITING FOR TELEMETRY ...");
}

UIEngine::~UIEngine() {
    free(buffer_);
}

bool UIEngine::init() {
    int w = EPD_2in13_V4_WIDTH;   // 122 (short edge)
    int h = EPD_2in13_V4_HEIGHT;  // 250 (long edge)
    int bytes = ((w % 8 == 0) ? (w / 8) : (w / 8 + 1)) * h;

    buffer_ = static_cast<UBYTE *>(malloc(bytes));
    if (!buffer_) return false;

    // rotation=90 → horizontal: logical 250 wide, 122 tall
    Paint_NewImage(buffer_, w, h, 90, WHITE);
    Paint_SelectImage(buffer_);
    return true;
}

void UIEngine::sleep() {
    EPD_2in13_V4_Sleep();
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void UIEngine::draw_hline(int y, bool dotted) {
    LINE_STYLE style = dotted ? LINE_STYLE_DOTTED : LINE_STYLE_SOLID;
    Paint_DrawLine(0, y, CANVAS_W - 1, y, WHITE, DOT_PIXEL_1X1, style);
}

void UIEngine::draw_vline(int x, int y1, int y2) {
    Paint_DrawLine(x, y1, x, y2, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

// Pixel-accurate filled bar — no artifacts from rectangle helpers
void UIEngine::draw_bar(int x, int y, int w, int h, float pct) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    // outer border
    Paint_DrawRectangle(x, y, x + w - 1, y + h - 1,
                        WHITE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    int inner_w = w - 2;
    int fill    = static_cast<int>(inner_w * pct / 100.0f);

    // draw row by row to avoid library quirks
    for (int row = y + 1; row <= y + h - 2; row++) {
        if (fill > 0)
            Paint_DrawLine(x + 1, row, x + fill, row,
                           WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        if (fill < inner_w)
            Paint_DrawLine(x + 1 + fill, row, x + w - 2, row,
                           BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
}

// ---------------------------------------------------------------------------
// Section renderers
// ---------------------------------------------------------------------------

void UIEngine::draw_header(const SystemMetrics &m) {
    bool warn_cpu  = m.cpu_pct    > 85.0f;
    bool warn_temp = m.cpu_temp_c > 70.0f;
    bool warn_mem  = m.mem_pct    > 85;
    bool net_up    = (m.ip != "NO_LINK");

    Paint_DrawString_EN(2, Y_HEADER_TOP + 2,
                        "SENTINEL-1", &Font8, WHITE, BLACK);

    char status[48];
    snprintf(status, sizeof(status), "CPU:%s MEM:%s NET:%s TMP:%s",
             warn_cpu  ? "WRN" : "OK ",
             warn_mem  ? "WRN" : "OK ",
             net_up    ? "OK " : "DWN",
             warn_temp ? "WRN" : "OK ");
    Paint_DrawString_EN(95, Y_HEADER_TOP + 2, status, &Font8, WHITE, BLACK);

    draw_hline(Y_H0);
}

void UIEngine::draw_metrics(const SystemMetrics &m) {
    // TIME | DATE
    char tl[24], tr[24];
    snprintf(tl, sizeof(tl), "TIME  %s", m.time_str);
    snprintf(tr, sizeof(tr), "DATE  %s", m.date_str);
    Paint_DrawString_EN(2,          Y_TIME_TXT, tl, &Font8, WHITE, BLACK);
    draw_vline(X_SPLIT, Y_H0, Y_H1);
    Paint_DrawString_EN(X_SPLIT + 3, Y_TIME_TXT, tr, &Font8, WHITE, BLACK);
    draw_hline(Y_H1, true);

    // CPU | TEMP
    char cl[24], cr[24];
    snprintf(cl, sizeof(cl), "CPU   %5.1f%%", m.cpu_pct);
    snprintf(cr, sizeof(cr), "TEMP  %5.1fC",  m.cpu_temp_c);
    Paint_DrawString_EN(2,          Y_CPU_TXT, cl, &Font8, WHITE, BLACK);
    draw_vline(X_SPLIT, Y_H1, Y_H2);
    Paint_DrawString_EN(X_SPLIT + 3, Y_CPU_TXT, cr, &Font8, WHITE, BLACK);
    draw_hline(Y_H2, true);

    // MEM — full width: label + bar
    char ml[40];
    snprintf(ml, sizeof(ml), "MEM  %dM / %dM   %d%%",
             m.mem_used_mb, m.mem_total_mb, m.mem_pct);
    Paint_DrawString_EN(2, Y_MEM_TXT, ml, &Font8, WHITE, BLACK);
    draw_bar(2, Y_MEM_BAR, CANVAS_W - 4, Y_MEM_BAR_H,
             static_cast<float>(m.mem_pct));
    draw_hline(Y_H3);

    // UP | IP
    char ul[24], ur[32];
    snprintf(ul, sizeof(ul), "UP    %s", m.uptime_str);
    snprintf(ur, sizeof(ur), "IP    %s", m.ip.c_str());
    Paint_DrawString_EN(2,           Y_UPIP_TXT, ul, &Font8, WHITE, BLACK);
    draw_vline(X_SPLIT, Y_H3, Y_H4);
    Paint_DrawString_EN(X_SPLIT + 3, Y_UPIP_TXT, ur, &Font8, WHITE, BLACK);
    draw_hline(Y_H4, true);
}

void UIEngine::push_terminal_line(const char *line) {
    for (int i = static_cast<int>(terminal_lines_.size()) - 1; i > 0; --i) {
        terminal_lines_[i] = terminal_lines_[i - 1];
    }
    terminal_lines_[0] = line;
}

void UIEngine::draw_terminal(const SystemMetrics &m) {
    bool warn_cpu  = m.cpu_pct    > 85.0f;
    bool warn_temp = m.cpu_temp_c > 70.0f;
    bool warn_mem  = m.mem_pct    > 85;
    bool net_up    = (m.ip != "NO_LINK");

    char line[64];
    if (warn_temp || warn_cpu || warn_mem) {
        snprintf(line, sizeof(line), "> %s ALERT %s",
                 m.time_str,
                 warn_temp ? "THERMAL_LIMIT" :
                 warn_cpu  ? "CPU_LOAD_HIGH" :
                             "MEM_PRESSURE");
    } else if (!net_up) {
        snprintf(line, sizeof(line), "> %s NET   DOWN DHCP_WAIT", m.time_str);
    } else {
        switch (frame_count_ % 16) {
        case 0:
            snprintf(line, sizeof(line), "> %s SYS   C=%4.1f%% T=%4.1fC M=%3d%%",
                     m.time_str, m.cpu_pct, m.cpu_temp_c, m.mem_pct);
            break;
        case 1:
            snprintf(line, sizeof(line), "> %s LOAD  %0.2f/%0.2f/%0.2f",
                     m.time_str, m.load1, m.load5, m.load15);
            break;
        case 2:
            snprintf(line, sizeof(line), "> %s PROC  %d/%d GOV=%s",
                     m.time_str, m.process_running, m.process_total, m.cpu_governor.c_str());
            break;
        case 3:
            snprintf(line, sizeof(line), "> %s CPU   F=%dMHz THR=%s",
                     m.time_str, m.cpu_freq_mhz, m.throttle_flags.c_str());
            break;
        case 4:
            snprintf(line, sizeof(line), "> %s THERM CPU=%4.1f GPU=%4.1f",
                     m.time_str, m.cpu_temp_c, m.gpu_temp_c);
            break;
        case 5:
            snprintf(line, sizeof(line), "> %s PWR   CORE=%.3fV", m.time_str, m.core_volt_v);
            break;
        case 6:
            snprintf(line, sizeof(line), "> %s RAM   F=%d A=%d C=%d B=%d",
                     m.time_str, m.mem_free_mb, m.mem_available_mb, m.mem_cached_mb, m.mem_buffers_mb);
            break;
        case 7:
            snprintf(line, sizeof(line), "> %s MEM   SWP=%d/%d DSK=%d%%",
                     m.time_str, m.swap_used_mb, m.swap_total_mb, m.disk_root_pct);
            break;
        case 8:
            snprintf(line, sizeof(line), "> %s SDIO  R=%lluk W=%lluk",
                     m.time_str, m.sd_read_kb, m.sd_write_kb);
            break;
        case 9:
            snprintf(line, sizeof(line), "> %s SD    LIFE=%s",
                     m.time_str, m.sd_life_time.empty() ? "unknown" : m.sd_life_time.c_str());
            break;
        case 10:
            if (m.wifi_rssi_dbm > -200) {
                snprintf(line, sizeof(line), "> %s WIFI  Q=%d R=%d N=%d",
                         m.time_str, m.wifi_link_quality, m.wifi_rssi_dbm, m.wifi_noise_dbm);
            } else {
                snprintf(line, sizeof(line), "> %s WIFI  R=N/A SSID=%.16s", m.time_str, m.ssid.c_str());
            }
            break;
        case 11:
            snprintf(line, sizeof(line), "> %s NET   LOSS=%0.1f RX/TX=%5.0f/%5.0f",
                     m.time_str, m.packet_loss_pct, m.net_rx_kbps, m.net_tx_kbps);
            break;
        case 12:
            snprintf(line, sizeof(line), "> %s LOOP  JIT=%dms MISS=%d",
                     m.time_str, m.heartbeat_jitter_ms, m.heartbeat_missed_ticks);
            break;
        case 13:
            snprintf(line, sizeof(line), "> %s ID    BT=%.17s SR=%.12s",
                     m.time_str, m.bt_addr.c_str(), m.pi_serial.c_str());
            break;
        case 14:
            snprintf(line, sizeof(line), "> %s BT    UP=%s PAIR=%d CONN=%d",
                     m.time_str, m.bt_up ? "Y" : "N", m.bt_paired_count, m.bt_connected_count);
            break;
        default:
            snprintf(line, sizeof(line), "> %s HW    %.14s REV=%.8s",
                     m.time_str, m.pi_model.c_str(), m.pi_revision.c_str());
            break;
        }
    }

    push_terminal_line(line);
    if (!terminal_seeded_) {
        char init1[64];
        char init2[64];
        snprintf(init1, sizeof(init1), "> %s LIVE TELEMETRY STREAM STARTED", m.time_str);
        snprintf(init2, sizeof(init2), "> %s REFRESH 1S PARTIAL ACTIVE", m.time_str);
        push_terminal_line(init1);
        push_terminal_line(init2);
        terminal_seeded_ = true;
    }

    Paint_DrawString_EN(2, Y_LOG_L0, terminal_lines_[0].c_str(), &Font8, WHITE, BLACK);
    Paint_DrawString_EN(2, Y_LOG_L1, terminal_lines_[1].c_str(), &Font8, WHITE, BLACK);
    Paint_DrawString_EN(2, Y_LOG_L2, terminal_lines_[2].c_str(), &Font8, WHITE, BLACK);
    Paint_DrawString_EN(2, Y_LOG_L3, terminal_lines_[3].c_str(), &Font8, WHITE, BLACK);
    Paint_DrawString_EN(2, Y_LOG_L4, terminal_lines_[4].c_str(), &Font8, WHITE, BLACK);
    Paint_DrawString_EN(2, Y_LOG_L5, terminal_lines_[5].c_str(), &Font8, WHITE, BLACK);
}

void UIEngine::render_shutdown(const SystemMetrics &m, const char *reason) {
    char line[64];
    char persist[64];
    snprintf(line, sizeof(line), "> %s SHUTDOWN SIGNAL=%s", m.time_str, reason ? reason : "UNKNOWN");
    snprintf(persist, sizeof(persist), "> %s LAST FRAME PERSISTED", m.time_str);
    push_terminal_line(line);
    push_terminal_line(persist);
    render(m);
    EPD_2in13_V4_Display_Base(buffer_);
}

// ---------------------------------------------------------------------------
// Public render — full frame
// ---------------------------------------------------------------------------

void UIEngine::render(const SystemMetrics &m) {
    ++frame_count_;
    Paint_Clear(BLACK);

    draw_header(m);
    draw_metrics(m);
    draw_terminal(m);

    const bool do_full_refresh =
        (frame_count_ == 1) || (frame_count_ % FULL_REFRESH_EVERY_N_FRAMES == 0);

    if (do_full_refresh) {
        EPD_2in13_V4_Display_Base(buffer_);
        return;
    }

    // Partial refresh removes the full-screen blink between updates.
    EPD_2in13_V4_Display_Partial(buffer_);
}
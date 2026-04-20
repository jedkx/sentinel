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

static constexpr int Y_NET_TXT     =  60;
static constexpr int Y_H4          =  70; // dotted

static constexpr int Y_UP_TXT      =  73;
static constexpr int Y_H5          =  83; // solid

static constexpr int Y_LOG_HDR     =  86;
static constexpr int Y_H6          =  95; // dotted
static constexpr int Y_LOG_L0      =  97;
static constexpr int Y_LOG_L1      = 106;
static constexpr int Y_LOG_L2      = 115; // (may be clipped, 122-115=7px)

static constexpr int Y_H7          = 112; // solid — footer separator
static constexpr int Y_FOOTER_TXT  = 114;

// Grid: vertical separator between left/right columns
static constexpr int X_SPLIT       = 124;

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

UIEngine::UIEngine() : buffer_(nullptr), seq_(0) {}

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
}

void UIEngine::draw_network(const SystemMetrics &m) {
    // IFACE | IP
    char nl[24], nr[32];
    snprintf(nl, sizeof(nl), "IFACE %-6s", m.iface.c_str());
    snprintf(nr, sizeof(nr), "IP    %s",   m.ip.c_str());
    Paint_DrawString_EN(2,          Y_NET_TXT, nl, &Font8, WHITE, BLACK);
    draw_vline(X_SPLIT, Y_H3, Y_H4);
    Paint_DrawString_EN(X_SPLIT + 3, Y_NET_TXT, nr, &Font8, WHITE, BLACK);
    draw_hline(Y_H4, true);

    // UPTIME | NET speed
    char ul[24], ur[24];
    int up_h = static_cast<int>(m.uptime_s / 3600);
    int up_m = static_cast<int>((m.uptime_s % 3600) / 60);
    snprintf(ul, sizeof(ul), "UP    %02dh%02dm", up_h, up_m);
    snprintf(ur, sizeof(ur), "RX/TX %.0f/%.0fK",
             m.net_rx_kbps, m.net_tx_kbps);
    Paint_DrawString_EN(2,          Y_UP_TXT, ul, &Font8, WHITE, BLACK);
    draw_vline(X_SPLIT, Y_H4, Y_H5);
    Paint_DrawString_EN(X_SPLIT + 3, Y_UP_TXT, ur, &Font8, WHITE, BLACK);
    draw_hline(Y_H5);
}

void UIEngine::draw_log(const SystemMetrics &m) {
    bool warn_cpu  = m.cpu_pct    > 85.0f;
    bool warn_temp = m.cpu_temp_c > 70.0f;
    bool warn_mem  = m.mem_pct    > 85;
    bool net_up    = (m.ip != "NO_LINK");
    int  up_h      = static_cast<int>(m.uptime_s / 3600);
    int  up_m      = static_cast<int>((m.uptime_s % 3600) / 60);

    char hdr[16];
    snprintf(hdr, sizeof(hdr), "EVT LOG  SEQ:%04d", seq_);
    Paint_DrawString_EN(2, Y_LOG_HDR, hdr, &Font8, WHITE, BLACK);
    draw_hline(Y_H6, true);

    char ev0[48], ev1[48], ev2[52];
    snprintf(ev0, sizeof(ev0), "> T+00:00  BOOT OK  KERNEL READY");
    snprintf(ev1, sizeof(ev1), "> T+00:01  NET %-4s  IFACE %s",
             net_up ? "UP" : "DOWN", m.iface.c_str());
    snprintf(ev2, sizeof(ev2), "> T+%02d:%02d  %s",
             up_h, up_m,
             warn_temp ? "!! THERMAL LIMIT EXCEEDED" :
             warn_cpu  ? "!! HIGH CPU LOAD DETECTED" :
             warn_mem  ? "!! MEMORY PRESSURE HIGH  " :
                         "ALL SUBSYSTEMS NOMINAL   ");

    Paint_DrawString_EN(2, Y_LOG_L0, ev0, &Font8, WHITE, BLACK);
    Paint_DrawString_EN(2, Y_LOG_L1, ev1, &Font8, WHITE, BLACK);
}

void UIEngine::draw_footer(const SystemMetrics &m) {
    bool warn = m.cpu_pct > 85.0f || m.cpu_temp_c > 70.0f || m.mem_pct > 85;
    int  up_h = static_cast<int>(m.uptime_s / 3600);
    int  up_m = static_cast<int>((m.uptime_s % 3600) / 60);

    draw_hline(Y_H7);

    char left[40], right[12];
    snprintf(left,  sizeof(left),  "STA:%-7s  T+%02dh%02dm",
             warn ? "WARN" : "NOMINAL", up_h, up_m);
    snprintf(right, sizeof(right), "v1.0.0");

    Paint_DrawString_EN(2,   Y_FOOTER_TXT, left,  &Font8, WHITE, BLACK);
    Paint_DrawString_EN(215, Y_FOOTER_TXT, right, &Font8, WHITE, BLACK);
}

// ---------------------------------------------------------------------------
// Public render — full frame
// ---------------------------------------------------------------------------

void UIEngine::render(const SystemMetrics &m) {
    ++seq_;
    Paint_Clear(BLACK);

    draw_header(m);
    draw_metrics(m);
    draw_network(m);
    draw_log(m);
    draw_footer(m);

    EPD_2in13_V4_Display_Base(buffer_);
}
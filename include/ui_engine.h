#pragma once

#include <array>
#include <string>

#include "telemetry.h"

#ifdef HAVE_WAVESHARE
extern "C" {
    #include "EPD_2in13_V4.h"
    #include "GUI_Paint.h"
}
#else
using UBYTE = unsigned char;
#endif

// Canvas dimensions when mounted horizontally (rotation=90)
static constexpr int CANVAS_W = 250;
static constexpr int CANVAS_H = 122;

class UIEngine {
public:
    UIEngine();
    ~UIEngine();

    bool init();
    void render(const SystemMetrics &m);
    void render_shutdown(const SystemMetrics &m, const char *reason);
    void sleep();

private:
    UBYTE *buffer_;
    int    frame_count_;
    int    full_refresh_every_n_frames_;
    std::array<std::string, 6> terminal_lines_;
    bool terminal_seeded_;
    std::string last_event_line_;

    void draw_hline(int y, bool dotted = false);
    void draw_vline(int x, int y1, int y2);
    void draw_bar(int x, int y, int w, int h, float pct);
    void draw_header(const SystemMetrics &m);
    void draw_metrics(const SystemMetrics &m);
    void draw_terminal(const SystemMetrics &m);
    void push_terminal_line(const char *line);
};
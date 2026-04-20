#pragma once

#include "telemetry.h"

extern "C" {
    #include "EPD_2in13_V4.h"
    #include "GUI_Paint.h"
}

// Canvas dimensions when mounted horizontally (rotation=90)
static constexpr int CANVAS_W = 250;
static constexpr int CANVAS_H = 122;

class UIEngine {
public:
    UIEngine();
    ~UIEngine();

    bool init();
    void render(const SystemMetrics &m);
    void sleep();

private:
    UBYTE *buffer_;
    int    seq_;

    void draw_hline(int y, bool dotted = false);
    void draw_vline(int x, int y1, int y2);
    void draw_bar(int x, int y, int w, int h, float pct);
    void draw_header(const SystemMetrics &m);
    void draw_metrics(const SystemMetrics &m);
    void draw_network(const SystemMetrics &m);
    void draw_log(const SystemMetrics &m);
    void draw_footer(const SystemMetrics &m);
};
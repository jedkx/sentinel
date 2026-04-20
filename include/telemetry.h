#pragma once

#include <string>

struct SystemMetrics {
    float cpu_pct;       // 0.0 - 100.0
    float cpu_temp_c;
    int   mem_used_mb;
    int   mem_total_mb;
    int   mem_pct;
    float net_rx_kbps;   // receive KB/s
    float net_tx_kbps;   // transmit KB/s
    std::string ip;
    std::string iface;
    std::string hostname;
    long  uptime_s;
    char  time_str[12];  // HH:MM:SS
    char  date_str[14];  // YYYY-MM-DD
};

void collect_metrics(SystemMetrics &m);
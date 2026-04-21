#pragma once

#include <string>

struct SystemMetrics {
    float cpu_pct;       // 0.0 - 100.0
    float cpu_temp_c;
    float gpu_temp_c;
    float core_volt_v;
    int   mem_used_mb;
    int   mem_total_mb;
    int   mem_pct;
    int   mem_free_mb;
    int   mem_available_mb;
    int   mem_cached_mb;
    int   mem_buffers_mb;
    int   swap_total_mb;
    int   swap_used_mb;
    std::string ip;
    std::string iface;
    std::string ssid;
    std::string hostname;
    std::string cpu_governor;
    float load1;
    float load5;
    float load15;
    int   process_running;
    int   process_total;
    int   disk_root_pct;
    unsigned long long sd_read_kb;
    unsigned long long sd_write_kb;
    std::string sd_life_time;
    int   cpu_freq_mhz;
    std::string throttle_flags;
    int   wifi_rssi_dbm;
    int   wifi_link_quality;
    int   wifi_noise_dbm;
    float packet_loss_pct;
    bool  bt_up;
    std::string bt_addr;
    int   bt_paired_count;
    int   bt_connected_count;
    std::string pi_model;
    std::string pi_serial;
    std::string pi_revision;
    double uptime_raw_s;
    int   heartbeat_jitter_ms;
    int   heartbeat_missed_ticks;
    long  uptime_s;
    char  uptime_str[12]; // HH:MM:SS
    char  time_str[12];  // HH:MM:SS
    char  date_str[14];  // YYYY-MM-DD
    char  boot_time_str[12]; // HH:MM:SS
    std::string event_line;
};

void collect_metrics(SystemMetrics &m);
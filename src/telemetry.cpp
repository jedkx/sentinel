#include "telemetry.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static float read_cpu_usage() {
    static long prev_idle = 0, prev_total = 0;

    std::ifstream f("/proc/stat");
    if (!f.is_open()) return 0.0f;

    std::string tag;
    long user, nice, sys, idle, iowait, irq, softirq;
    f >> tag >> user >> nice >> sys >> idle >> iowait >> irq >> softirq;

    long idle_now  = idle + iowait;
    long total_now = user + nice + sys + idle + iowait + irq + softirq;
    long di = idle_now  - prev_idle;
    long dt = total_now - prev_total;

    prev_idle  = idle_now;
    prev_total = total_now;

    if (dt == 0) return 0.0f;
    return 100.0f * (1.0f - static_cast<float>(di) / dt);
}

static float read_cpu_temp() {
    std::ifstream f("/sys/class/thermal/thermal_zone0/temp");
    if (!f.is_open()) return 0.0f;
    int raw = 0;
    f >> raw;
    return raw / 1000.0f;
}

static void read_memory(int &used_mb, int &total_mb) {
    struct sysinfo si;
    sysinfo(&si);
    total_mb = static_cast<int>(si.totalram / 1024 / 1024);
    int free_mb = static_cast<int>((si.freeram + si.bufferram) / 1024 / 1024);
    used_mb = total_mb - free_mb;
}

// Returns KB/s for rx and tx on the first non-loopback interface.
// Reads /proc/net/dev twice with a short sleep in between.
static void read_net_speed(const std::string &iface,
                           float &rx_kbps, float &tx_kbps) {
    auto read_bytes = [&](unsigned long long &rx, unsigned long long &tx) {
        std::ifstream f("/proc/net/dev");
        std::string line;
        while (std::getline(f, line)) {
            if (line.find(iface) == std::string::npos) continue;
            // format: iface: rx_bytes ... tx_bytes ...
            const char *p = line.c_str();
            while (*p && *p != ':') p++;
            if (*p) p++;
            sscanf(p, "%llu %*u %*u %*u %*u %*u %*u %*u %llu", &rx, &tx);
            return;
        }
        rx = tx = 0;
    };

    unsigned long long rx1, tx1, rx2, tx2;
    read_bytes(rx1, tx1);
    usleep(500000); // 500 ms sample window
    read_bytes(rx2, tx2);

    rx_kbps = static_cast<float>(rx2 - rx1) / 512.0f; // bytes -> KB/s over 0.5s
    tx_kbps = static_cast<float>(tx2 - tx1) / 512.0f;
}

static std::string read_iface() {
    struct ifaddrs *ifa = nullptr;
    getifaddrs(&ifa);
    for (auto *i = ifa; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (std::string(i->ifa_name) == "lo") continue;
        std::string name(i->ifa_name);
        freeifaddrs(ifa);
        return name;
    }
    if (ifa) freeifaddrs(ifa);
    return "none";
}

static std::string read_ip(const std::string &iface) {
    struct ifaddrs *ifa = nullptr;
    getifaddrs(&ifa);
    for (auto *i = ifa; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (std::string(i->ifa_name) != iface) continue;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,
                  &reinterpret_cast<struct sockaddr_in *>(i->ifa_addr)->sin_addr,
                  buf, sizeof(buf));
        freeifaddrs(ifa);
        return std::string(buf);
    }
    if (ifa) freeifaddrs(ifa);
    return "NO_LINK";
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void collect_metrics(SystemMetrics &m) {
    m.cpu_pct      = read_cpu_usage();
    m.cpu_temp_c   = read_cpu_temp();

    read_memory(m.mem_used_mb, m.mem_total_mb);
    m.mem_pct = (m.mem_total_mb > 0)
                ? m.mem_used_mb * 100 / m.mem_total_mb
                : 0;

    m.iface = read_iface();
    m.ip    = read_ip(m.iface);
    read_net_speed(m.iface, m.net_rx_kbps, m.net_tx_kbps);

    char hbuf[64] = "sentinel";
    gethostname(hbuf, sizeof(hbuf));
    m.hostname = std::string(hbuf);

    struct sysinfo si;
    sysinfo(&si);
    m.uptime_s = si.uptime;

    time_t now = time(nullptr);
    tm *lt = localtime(&now);
    strftime(m.time_str, sizeof(m.time_str), "%H:%M:%S", lt);
    strftime(m.date_str, sizeof(m.date_str), "%Y-%m-%d", lt);
}
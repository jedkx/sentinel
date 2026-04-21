#include "telemetry.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string run_command(const char *cmd);

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

static float parse_first_float(const std::string &s) {
    bool seen_digit = false;
    std::string token;
    token.reserve(16);
    for (char c : s) {
        const bool digit = (c >= '0' && c <= '9');
        if (digit || (c == '.' && seen_digit)) {
            token.push_back(c);
            if (digit) seen_digit = true;
            continue;
        }
        if (seen_digit) break;
    }
    if (!token.empty()) {
        return std::stof(token);
    }
    return 0.0f;
}

static float read_gpu_temp() {
    const std::string out = run_command("vcgencmd measure_temp 2>/dev/null");
    return parse_first_float(out);
}

static float read_core_volt() {
    const std::string out = run_command("vcgencmd measure_volts core 2>/dev/null");
    return parse_first_float(out);
}

static void read_memory(int &used_mb, int &total_mb) {
    std::ifstream f("/proc/meminfo");
    if (f.is_open()) {
        long total_kb = 0;
        long available_kb = 0;
        std::string key;
        long value = 0;
        std::string unit;

        while (f >> key >> value >> unit) {
            if (key == "MemTotal:") total_kb = value;
            if (key == "MemAvailable:") available_kb = value;
            if (total_kb > 0 && available_kb > 0) break;
        }

        if (total_kb > 0 && available_kb > 0) {
            total_mb = static_cast<int>(total_kb / 1024);
            used_mb = static_cast<int>((total_kb - available_kb) / 1024);
            return;
        }
    }

    // Fallback path when /proc/meminfo is unavailable.
    struct sysinfo si;
    sysinfo(&si);
    total_mb = static_cast<int>(si.totalram / 1024 / 1024);
    int free_mb = static_cast<int>(si.freeram / 1024 / 1024);
    used_mb = total_mb - free_mb;
}

static void read_memory_details(SystemMetrics &m) {
    std::ifstream f("/proc/meminfo");
    long total_kb = 0, free_kb = 0, avail_kb = 0, cached_kb = 0, buffers_kb = 0;
    std::string key, unit;
    long value = 0;
    while (f >> key >> value >> unit) {
        if (key == "MemTotal:") total_kb = value;
        else if (key == "MemFree:") free_kb = value;
        else if (key == "MemAvailable:") avail_kb = value;
        else if (key == "Cached:") cached_kb = value;
        else if (key == "Buffers:") buffers_kb = value;
    }
    m.mem_free_mb = static_cast<int>(free_kb / 1024);
    m.mem_available_mb = static_cast<int>(avail_kb / 1024);
    m.mem_cached_mb = static_cast<int>(cached_kb / 1024);
    m.mem_buffers_mb = static_cast<int>(buffers_kb / 1024);
    if (total_kb > 0) m.mem_total_mb = static_cast<int>(total_kb / 1024);
}

static void read_swap(SystemMetrics &m) {
    std::ifstream f("/proc/swaps");
    std::string line;
    std::getline(f, line);
    long total_kb = 0;
    long used_kb = 0;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string filename, type;
        long size = 0, used = 0;
        int prio = 0;
        if (iss >> filename >> type >> size >> used >> prio) {
            total_kb += size;
            used_kb += used;
        }
    }
    m.swap_total_mb = static_cast<int>(total_kb / 1024);
    m.swap_used_mb = static_cast<int>(used_kb / 1024);
}

static void read_iface_ip(std::string &iface, std::string &ip) {
    iface = "none";
    ip = "NO_LINK";
    struct ifaddrs *ifa = nullptr;
    if (getifaddrs(&ifa) != 0) return;
    for (auto *i = ifa; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (std::string(i->ifa_name) == "lo") continue;
        iface = i->ifa_name;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,
                  &reinterpret_cast<struct sockaddr_in *>(i->ifa_addr)->sin_addr,
                  buf, sizeof(buf));
        ip = std::string(buf);
        freeifaddrs(ifa);
        return;
    }
    if (ifa) freeifaddrs(ifa);
}

static std::string run_command(const char *cmd) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return "";

    std::string out;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        out += buffer;
    }
    pclose(pipe);
    return out;
}

static std::string trim(std::string s);
static std::string read_file_trimmed(const char *path);

static void read_load_and_process(SystemMetrics &m) {
    std::ifstream f("/proc/loadavg");
    std::string proc_field;
    if (!(f >> m.load1 >> m.load5 >> m.load15 >> proc_field)) {
        m.load1 = m.load5 = m.load15 = 0.0f;
        m.process_running = 0;
        m.process_total = 0;
        return;
    }
    size_t slash = proc_field.find('/');
    if (slash != std::string::npos) {
        m.process_running = std::stoi(proc_field.substr(0, slash));
        m.process_total = std::stoi(proc_field.substr(slash + 1));
    } else {
        m.process_running = 0;
        m.process_total = 0;
    }
}

static int read_disk_root_pct() {
    struct statvfs s {};
    if (statvfs("/", &s) != 0 || s.f_blocks == 0) return 0;
    const unsigned long long used = s.f_blocks - s.f_bavail;
    return static_cast<int>((used * 100ULL) / s.f_blocks);
}

static int read_cpu_freq_mhz() {
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    long khz = 0;
    if (f >> khz) return static_cast<int>(khz / 1000);

    std::ifstream f2("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq");
    if (f2 >> khz) return static_cast<int>(khz / 1000);
    return 0;
}

static std::string read_cpu_governor() {
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    std::string gov;
    if (f >> gov) return gov;
    return "unknown";
}

static void read_wifi_signal(const std::string &iface, int &link, int &level, int &noise) {
    link = 0;
    level = -999;
    noise = -999;
    if (iface == "none") return;
    std::ifstream f("/proc/net/wireless");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find(iface + ":") == std::string::npos) continue;
        const char *p = std::strchr(line.c_str(), ':');
        if (!p) continue;
        ++p;
        int status = 0;
        float f_link = 0.0f, f_level = 0.0f, f_noise = 0.0f;
        if (std::sscanf(p, "%d %f %f %f", &status, &f_link, &f_level, &f_noise) == 4) {
            link = static_cast<int>(f_link);
            level = static_cast<int>(f_level);
            noise = static_cast<int>(f_noise);
            return;
        }
    }
}

static void read_sd_stats(SystemMetrics &m) {
    m.sd_read_kb = 0;
    m.sd_write_kb = 0;
    std::ifstream f("/sys/block/mmcblk0/stat");
    if (!f.is_open()) return;
    unsigned long long fields[11] = {};
    for (int i = 0; i < 11; ++i) {
        if (!(f >> fields[i])) return;
    }
    m.sd_read_kb = (fields[2] * 512ULL) / 1024ULL;
    m.sd_write_kb = (fields[6] * 512ULL) / 1024ULL;
}

static std::string read_sd_life_time() {
    std::string life = read_file_trimmed("/sys/block/mmcblk0/device/life_time");
    if (life.empty()) life = "unknown";
    return life;
}

static std::string read_file_trimmed(const char *path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    for (char &c : s) if (c == '\0') c = ' ';
    return trim(s);
}

static void read_hardware_identity(SystemMetrics &m) {
    m.pi_model = read_file_trimmed("/sys/firmware/devicetree/base/model");
    m.pi_serial = "unknown";
    m.pi_revision = "unknown";
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("Serial") == 0) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) m.pi_serial = trim(line.substr(pos + 1));
        } else if (line.find("Revision") == 0) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) m.pi_revision = trim(line.substr(pos + 1));
        }
    }
}

static void read_uptime_raw(SystemMetrics &m) {
    std::ifstream f("/proc/uptime");
    if (!(f >> m.uptime_raw_s)) m.uptime_raw_s = 0.0;
}

static void read_bluetooth(SystemMetrics &m) {
    m.bt_up = false;
    m.bt_addr = "n/a";
    m.bt_paired_count = 0;
    m.bt_connected_count = 0;

    std::string addr = read_file_trimmed("/sys/class/bluetooth/hci0/address");
    if (!addr.empty()) {
        m.bt_addr = addr;
        std::string state = read_file_trimmed("/sys/class/bluetooth/hci0/operstate");
        m.bt_up = (state == "up");
    }
}

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
    return s.substr(start);
}

static std::string read_ssid(const std::string &iface) {
    if (iface == "none") return "NO_LINK";

    std::string cmd = "iwgetid -r " + iface + " 2>/dev/null";
    std::string ssid = trim(run_command(cmd.c_str()));
    if (!ssid.empty()) return ssid;

    cmd = "iw dev " + iface + " link 2>/dev/null";
    const std::string link = run_command(cmd.c_str());
    const std::string marker = "SSID:";
    const std::size_t pos = link.find(marker);
    if (pos != std::string::npos) {
        std::size_t start = pos + marker.size();
        while (start < link.size() && (link[start] == ' ' || link[start] == '\t')) ++start;
        std::size_t end = link.find_first_of("\r\n", start);
        if (end == std::string::npos) end = link.size();
        return trim(link.substr(start, end - start));
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void collect_metrics(SystemMetrics &m) {
    time_t now = time(nullptr);
    static time_t last_fast_ts = 0;
    static time_t last_medium_ts = 0;
    static time_t last_slow_ts = 0;
    static time_t last_very_slow_ts = 0;
    static bool hardware_cached = false;

    static SystemMetrics cache{};

    if (last_fast_ts == 0 || now - last_fast_ts >= 1) {
        cache.cpu_pct = read_cpu_usage();
        cache.cpu_temp_c = read_cpu_temp();
        read_memory(cache.mem_used_mb, cache.mem_total_mb);
        cache.mem_pct = (cache.mem_total_mb > 0)
                        ? cache.mem_used_mb * 100 / cache.mem_total_mb
                        : 0;
        if (cache.mem_pct < 0) cache.mem_pct = 0;
        if (cache.mem_pct > 100) cache.mem_pct = 100;

        read_iface_ip(cache.iface, cache.ip);
        read_load_and_process(cache);
        cache.uptime_raw_s = 0.0;
        read_uptime_raw(cache);
        last_fast_ts = now;
    }

    if (last_medium_ts == 0 || now - last_medium_ts >= 10) {
        cache.cpu_freq_mhz = read_cpu_freq_mhz();
        cache.cpu_governor = read_cpu_governor();
        read_wifi_signal(cache.iface, cache.wifi_link_quality, cache.wifi_rssi_dbm, cache.wifi_noise_dbm);
        cache.throttle_flags = trim(run_command("vcgencmd get_throttled 2>/dev/null | awk -F= '{print $2}'"));
        if (cache.throttle_flags.empty()) cache.throttle_flags = "na";
        cache.gpu_temp_c = read_gpu_temp();
        if (cache.gpu_temp_c <= 0.0f) cache.gpu_temp_c = cache.cpu_temp_c;
        cache.core_volt_v = read_core_volt();
        last_medium_ts = now;
    }

    if (last_slow_ts == 0 || now - last_slow_ts >= 45) {
        cache.ssid = read_ssid(cache.iface);
        cache.disk_root_pct = read_disk_root_pct();
        read_sd_stats(cache);
        cache.sd_life_time = read_sd_life_time();
        read_memory_details(cache);
        read_swap(cache);
        last_slow_ts = now;
    }

    if (last_very_slow_ts == 0 || now - last_very_slow_ts >= 120) {
        read_bluetooth(cache);
        if (!hardware_cached) {
            read_hardware_identity(cache);
            hardware_cached = true;
        }
        last_very_slow_ts = now;
    }

    static bool hostname_cached = false;
    if (!hostname_cached) {
        char hbuf[64] = "sentinel";
        gethostname(hbuf, sizeof(hbuf));
        cache.hostname = std::string(hbuf);
        hostname_cached = true;
    }

    struct sysinfo si;
    sysinfo(&si);
    cache.uptime_s = si.uptime;
    int up_h = static_cast<int>(cache.uptime_s / 3600);
    int up_m = static_cast<int>((cache.uptime_s % 3600) / 60);
    int up_s = static_cast<int>(cache.uptime_s % 60);
    snprintf(cache.uptime_str, sizeof(cache.uptime_str), "%02d:%02d:%02d", up_h, up_m, up_s);

    tm *lt = localtime(&now);
    strftime(cache.time_str, sizeof(cache.time_str), "%H:%M:%S", lt);
    strftime(cache.date_str, sizeof(cache.date_str), "%Y-%m-%d", lt);

    time_t boot_ts = now - cache.uptime_s;
    tm *bt = localtime(&boot_ts);
    strftime(cache.boot_time_str, sizeof(cache.boot_time_str), "%H:%M:%S", bt);

    cache.packet_loss_pct = -1.0f;

    m = cache;
}
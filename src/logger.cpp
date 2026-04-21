#include "logger.h"

#include <ctime>
#include <utility>

Logger::Logger(std::string preferred_path)
    : fp_(nullptr), preferred_path_(std::move(preferred_path)) {}

Logger::~Logger() {
    if (fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
}

bool Logger::open_at(const std::string &path) {
    FILE *f = fopen(path.c_str(), "a");
    if (!f) {
        return false;
    }
    fp_ = f;
    path_ = path;
    return true;
}

bool Logger::init() {
    if (open_at(preferred_path_)) {
        return true;
    }
    return open_at("/tmp/sentinel.log");
}

bool Logger::reopen() {
    if (path_.empty()) {
        return init();
    }

    FILE *new_fp = fopen(path_.c_str(), "a");
    if (!new_fp) {
        return false;
    }

    if (fp_) {
        fclose(fp_);
    }
    fp_ = new_fp;
    return true;
}

void Logger::write(const char *level, const std::string &msg) {
    const std::time_t now = std::time(nullptr);
    std::tm local_tm;
    localtime_r(&now, &local_tm);

    char ts[24];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &local_tm);

    fprintf(stdout, "%s [%s] %s\n", ts, level, msg.c_str());
    fflush(stdout);

    if (fp_) {
        fprintf(fp_, "%s [%s] %s\n", ts, level, msg.c_str());
        fflush(fp_);
    }
}

void Logger::info(const std::string &msg) {
    write("INF", msg);
}

void Logger::warn(const std::string &msg) {
    write("WRN", msg);
}

void Logger::error(const std::string &msg) {
    write("ERR", msg);
}

const std::string &Logger::path() const {
    return path_;
}

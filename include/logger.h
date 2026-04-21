#pragma once

#include <cstdio>
#include <string>

class Logger {
public:
    explicit Logger(std::string preferred_path);
    ~Logger();

    bool init();
    bool reopen();

    void info(const std::string &msg);
    void warn(const std::string &msg);
    void error(const std::string &msg);

    const std::string &path() const;

private:
    FILE *fp_;
    std::string preferred_path_;
    std::string path_;

    bool open_at(const std::string &path);
    void write(const char *level, const std::string &msg);
};

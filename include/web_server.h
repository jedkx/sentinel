#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "telemetry.h"

struct WebConfig {
    bool enabled;
    std::string bind_addr;
    int port;
    std::string token;
};

class WebServer {
public:
    WebServer();
    ~WebServer();

    bool start(const WebConfig &cfg);
    void stop();

    void update_metrics(const SystemMetrics &m);
    void push_event(const std::string &line);

private:
    bool running_;
    int server_fd_;
    WebConfig cfg_;
    std::thread server_thread_;

    mutable std::mutex mu_;
    SystemMetrics last_metrics_;
    std::deque<std::string> events_;

    static constexpr std::size_t MAX_EVENTS = 500;

    void serve_loop();
    void handle_client(int client_fd);

    std::string build_health_json() const;
    std::string build_state_json() const;
    std::string build_events_json(int limit) const;
    std::string build_config_json() const;
    std::string build_config_files_json() const;
    std::string build_logfile_json(int lines) const;
    std::string build_ui_html() const;
    std::string handle_config_update(const std::string &body) const;

    bool authorized(const std::string &request) const;
};

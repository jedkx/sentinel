#include "external_log_watcher.h"

#include <fstream>
#include <sstream>

std::string ExternalLogWatcher::trim(const std::string &s) {
    std::size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }

    std::size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        --end;
    }

    return s.substr(start, end - start);
}

void ExternalLogWatcher::configure_from_spec(const std::string &spec) {
    sources_.clear();

    std::stringstream ss(spec);
    std::string token;

    while (std::getline(ss, token, ',')) {
        token = trim(token);
        if (token.empty()) {
            continue;
        }

        Source s;
        s.offset = 0;
        s.primed = false;

        const std::size_t eq = token.find('=');
        if (eq == std::string::npos) {
            s.path = token;
            const std::size_t slash = token.find_last_of('/');
            s.name = (slash == std::string::npos) ? token : token.substr(slash + 1);
        } else {
            s.name = trim(token.substr(0, eq));
            s.path = trim(token.substr(eq + 1));
            if (s.name.empty()) {
                s.name = s.path;
            }
        }

        if (!s.path.empty()) {
            sources_.push_back(s);
        }
    }
}

std::vector<ExternalEvent> ExternalLogWatcher::poll(std::size_t max_events) {
    std::vector<ExternalEvent> events;

    for (auto &src : sources_) {
        std::ifstream f(src.path);
        if (!f.is_open()) {
            continue;
        }

        f.seekg(0, std::ios::end);
        std::streamoff end = f.tellg();
        if (end < 0) {
            continue;
        }

        if (!src.primed) {
            src.offset = end;
            src.primed = true;
            continue;
        }

        if (src.offset > end) {
            // File rotated or truncated.
            src.offset = 0;
        }

        f.seekg(src.offset, std::ios::beg);

        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            events.push_back({src.name, line});
            if (events.size() >= max_events) {
                src.offset = f.tellg();
                if (src.offset < 0) {
                    src.offset = end;
                }
                return events;
            }
        }

        src.offset = f.tellg();
        if (src.offset < 0) {
            src.offset = end;
        }
    }

    return events;
}

bool ExternalLogWatcher::empty() const {
    return sources_.empty();
}

std::string ExternalLogWatcher::describe_sources() const {
    std::string out;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += sources_[i].name;
    }
    return out;
}

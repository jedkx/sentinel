#pragma once

#include <ios>
#include <string>
#include <vector>

struct ExternalEvent {
    std::string source;
    std::string line;
};

class ExternalLogWatcher {
public:
    // Spec format: "name=/path/to/log,/other/path.log"
    void configure_from_spec(const std::string &spec);

    std::vector<ExternalEvent> poll(std::size_t max_events);

    bool empty() const;
    std::string describe_sources() const;

private:
    struct Source {
        std::string name;
        std::string path;
        std::streamoff offset;
        bool primed;
    };

    std::vector<Source> sources_;

    static std::string trim(const std::string &s);
};

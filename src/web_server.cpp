#include "web_server.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += ' ';
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

std::string http_response(int code, const char *status,
                          const char *content_type,
                          const std::string &body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << ' ' << status << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "Cache-Control: no-store\r\n\r\n";
    oss << body;
    return oss.str();
}

int parse_limit_query(const std::string &path, int defv) {
    const std::string key = "limit=";
    std::size_t p = path.find('?');
    if (p == std::string::npos) return defv;
    std::string query = path.substr(p + 1);
    p = query.find(key);
    if (p == std::string::npos) return defv;
    p += key.size();
    int v = std::atoi(query.c_str() + static_cast<int>(p));
    if (v < 1) return 1;
    if (v > 500) return 500;
    return v;
}

int parse_lines_query(const std::string &path, int defv) {
    const std::string key = "lines=";
    std::size_t p = path.find('?');
    if (p == std::string::npos) return defv;
    std::string query = path.substr(p + 1);
    p = query.find(key);
    if (p == std::string::npos) return defv;
    p += key.size();
    int v = std::atoi(query.c_str() + static_cast<int>(p));
    if (v < 10) return 10;
    if (v > 2000) return 2000;
    return v;
}

std::string shell_escape_single_quoted(const std::string &v) {
    std::string out = "'";
    for (char c : v) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::string trim_copy(const std::string &s) {
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    std::size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

bool valid_env_key(const std::string &k) {
    if (k.empty() || k.size() > 64) return false;
    for (char c : k) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

std::string run_capture(const std::string &cmd) {
    std::array<char, 512> buf {};
    std::string out;
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) return out;
    while (fgets(buf.data(), static_cast<int>(buf.size()), fp)) {
        out += buf.data();
    }
    pclose(fp);
    return out;
}

} // namespace

WebServer::WebServer()
    : running_(false), server_fd_(-1), cfg_{false, "127.0.0.1", 9090, ""} {}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start(const WebConfig &cfg) {
    if (running_) return true;
    cfg_ = cfg;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        return false;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg_.port));
    if (inet_pton(AF_INET, cfg_.bind_addr.c_str(), &addr.sin_addr) != 1) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (bind(server_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (listen(server_fd_, 16) != 0) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    server_thread_ = std::thread(&WebServer::serve_loop, this);
    return true;
}

void WebServer::stop() {
    if (!running_) return;
    running_ = false;
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void WebServer::update_metrics(const SystemMetrics &m) {
    std::lock_guard<std::mutex> lock(mu_);
    last_metrics_ = m;
}

void WebServer::push_event(const std::string &line) {
    std::lock_guard<std::mutex> lock(mu_);
    events_.push_back(line);
    while (events_.size() > MAX_EVENTS) {
        events_.pop_front();
    }
}

void WebServer::serve_loop() {
    while (running_) {
        sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);
        int cfd = accept(server_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (cfd < 0) {
            if (!running_) break;
            continue;
        }
        handle_client(cfd);
        close(cfd);
    }
}

bool WebServer::authorized(const std::string &request) const {
    if (cfg_.token.empty()) {
        return true;
    }
    const std::string marker = "X-Sentinel-Token:";
    std::size_t pos = request.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    std::size_t line_end = request.find("\r\n", pos);
    if (line_end == std::string::npos) return false;

    std::string value = request.substr(pos + marker.size(), line_end - (pos + marker.size()));
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
    std::size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) --end;
    value = value.substr(start, end - start);

    return value == cfg_.token;
}

void WebServer::handle_client(int client_fd) {
    std::array<char, 4096> buf {};
    const ssize_t n = recv(client_fd, buf.data(), buf.size() - 1, 0);
    if (n <= 0) {
        return;
    }
    std::string req(buf.data(), static_cast<std::size_t>(n));

    std::size_t line_end = req.find("\r\n");
    if (line_end == std::string::npos) {
        std::string res = http_response(400, "Bad Request", "text/plain", "bad request\n");
        send(client_fd, res.data(), res.size(), 0);
        return;
    }

    std::string first_line = req.substr(0, line_end);
    std::istringstream iss(first_line);
    std::string method;
    std::string path;
    std::string version;
    iss >> method >> path >> version;

    if (method != "GET" && method != "POST") {
        std::string res = http_response(405, "Method Not Allowed", "text/plain", "method not allowed\n");
        send(client_fd, res.data(), res.size(), 0);
        return;
    }

    if (!authorized(req)) {
        std::string res = http_response(401, "Unauthorized", "application/json", "{\"error\":\"unauthorized\"}\n");
        send(client_fd, res.data(), res.size(), 0);
        return;
    }

    std::string base_path = path;
    std::size_t q = base_path.find('?');
    if (q != std::string::npos) {
        base_path = base_path.substr(0, q);
    }

    std::string res;
    if (base_path == "/health" && method == "GET") {
        res = http_response(200, "OK", "application/json", build_health_json());
    } else if (base_path == "/api/state" && method == "GET") {
        res = http_response(200, "OK", "application/json", build_state_json());
    } else if (base_path == "/api/events" && method == "GET") {
        int limit = parse_limit_query(path, 100);
        res = http_response(200, "OK", "application/json", build_events_json(limit));
    } else if (base_path == "/api/logfile" && method == "GET") {
        int lines = parse_lines_query(path, 200);
        res = http_response(200, "OK", "application/json", build_logfile_json(lines));
    } else if (base_path == "/api/config" && method == "GET") {
        res = http_response(200, "OK", "application/json", build_config_json());
    } else if (base_path == "/api/config/files" && method == "GET") {
        res = http_response(200, "OK", "application/json", build_config_files_json());
    } else if (base_path == "/api/config" && method == "POST") {
        std::size_t body_pos = req.find("\r\n\r\n");
        std::string body;
        if (body_pos != std::string::npos) {
            body = req.substr(body_pos + 4);
        }
        res = http_response(200, "OK", "application/json", handle_config_update(body));
    } else if ((base_path == "/ui" || base_path == "/") && method == "GET") {
        res = http_response(200, "OK", "text/html; charset=utf-8", build_ui_html());
    } else {
        res = http_response(404, "Not Found", "application/json", "{\"error\":\"not_found\"}\n");
    }

    send(client_fd, res.data(), res.size(), 0);
}

std::string WebServer::build_health_json() const {
    return "{\"ok\":true}\n";
}

std::string WebServer::build_state_json() const {
    std::lock_guard<std::mutex> lock(mu_);
    const SystemMetrics &m = last_metrics_;

    std::ostringstream oss;
    oss << "{";
    oss << "\"time\":\"" << json_escape(m.time_str) << "\",";
    oss << "\"date\":\"" << json_escape(m.date_str) << "\",";
    oss << "\"cpu_pct\":" << m.cpu_pct << ",";
    oss << "\"cpu_temp_c\":" << m.cpu_temp_c << ",";
    oss << "\"mem_pct\":" << m.mem_pct << ",";
    oss << "\"mem_used_mb\":" << m.mem_used_mb << ",";
    oss << "\"mem_total_mb\":" << m.mem_total_mb << ",";
    oss << "\"iface\":\"" << json_escape(m.iface) << "\",";
    oss << "\"ip\":\"" << json_escape(m.ip) << "\",";
    oss << "\"uptime\":\"" << json_escape(m.uptime_str) << "\",";
    oss << "\"event\":\"" << json_escape(m.event_line) << "\"";
    oss << "}\n";
    return oss.str();
}

std::string WebServer::build_events_json(int limit) const {
    std::lock_guard<std::mutex> lock(mu_);

    int sz = static_cast<int>(events_.size());
    int start = std::max(0, sz - limit);

    std::ostringstream oss;
    oss << "{\"events\":[";
    bool first = true;
    for (int i = start; i < sz; ++i) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << json_escape(events_[static_cast<std::size_t>(i)]) << "\"";
    }
    oss << "]}\n";
    return oss.str();
}

std::string WebServer::build_config_json() const {
    const char *dropin = "/etc/systemd/system/sentinel.service.d/override.conf";
    const std::string raw = run_capture(std::string("sh -lc \"test -f ") + dropin + " && cat " + dropin + "\"");

    std::ostringstream oss;
    oss << "{\"ok\":true,\"dropin_exists\":" << (raw.empty() ? "false" : "true") << ",\"env\":{";
    bool first = true;
    std::istringstream iss(raw);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim_copy(line);
        if (line.rfind("Environment=", 0) != 0) continue;
        std::string kv = line.substr(std::string("Environment=").size());
        if (kv.size() >= 2 && kv.front() == '"' && kv.back() == '"') {
            kv = kv.substr(1, kv.size() - 2);
        }
        std::size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string key = kv.substr(0, eq);
        std::string val = kv.substr(eq + 1);
        if (!valid_env_key(key)) continue;
        if (!first) oss << ",";
        first = false;
        oss << "\"" << json_escape(key) << "\":\"" << json_escape(val) << "\"";
    }
    oss << "}}\n";
    return oss.str();
}

std::string WebServer::handle_config_update(const std::string &body) const {
    std::string key;
    std::string value;
    std::size_t nl = body.find('\n');
    std::string first = (nl == std::string::npos) ? body : body.substr(0, nl);
    std::size_t eq = first.find('=');
    if (eq == std::string::npos) {
        return "{\"ok\":false,\"error\":\"expected KEY=VALUE payload\"}\n";
    }
    key = trim_copy(first.substr(0, eq));
    value = trim_copy(first.substr(eq + 1));
    if (!valid_env_key(key)) {
        return "{\"ok\":false,\"error\":\"invalid key\"}\n";
    }

    const std::string dropin_dir = "/etc/systemd/system/sentinel.service.d";
    const std::string dropin_file = dropin_dir + "/override.conf";
    const std::string escaped_value = shell_escape_single_quoted(value);

    std::ostringstream cmd;
    cmd << "sh -lc \""
        << "mkdir -p " << dropin_dir << " && "
        << "touch " << dropin_file << " && "
        << "grep -v '^Environment=\\\"" << key << "=' " << dropin_file << " > " << dropin_file << ".tmp || true && "
        << "mv " << dropin_file << ".tmp " << dropin_file << " && "
        << "printf 'Environment=\\\"" << key << "=" << "%s" << "\\\"\\n' " << escaped_value << " >> " << dropin_file << " && "
        << "systemctl daemon-reload && systemctl restart sentinel.service\"";
    int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        return "{\"ok\":false,\"error\":\"failed to apply config\"}\n";
    }
    return "{\"ok\":true}\n";
}

std::string WebServer::build_logfile_json(int lines) const {
    const char *log_path_env = std::getenv("SENTINEL_LOG_FILE");
    const std::string log_path = (log_path_env && *log_path_env) ? std::string(log_path_env)
                                                                  : std::string("/var/log/sentinel.log");
    const std::string cmd = std::string("sh -lc \"if [ -f ")
        + shell_escape_single_quoted(log_path)
        + " ]; then tail -n "
        + std::to_string(lines)
        + " "
        + shell_escape_single_quoted(log_path)
        + "; fi\"";
    const std::string text = run_capture(cmd);

    std::ostringstream oss;
    oss << "{\"ok\":true,\"path\":\"" << json_escape(log_path) << "\",\"lines\":" << lines
        << ",\"text\":\"" << json_escape(text) << "\"}\n";
    return oss.str();
}

std::string WebServer::build_config_files_json() const {
    const std::string dropin_path = "/etc/systemd/system/sentinel.service.d/override.conf";
    const std::string unit_path = "/etc/systemd/system/sentinel.service";
    const std::string dropin = run_capture(std::string("sh -lc \"test -f ")
        + dropin_path + " && cat " + dropin_path + "\"");
    const std::string unit = run_capture(std::string("sh -lc \"test -f ")
        + unit_path + " && cat " + unit_path + "\"");

    std::ostringstream oss;
    oss << "{";
    oss << "\"ok\":true,";
    oss << "\"dropin_path\":\"" << json_escape(dropin_path) << "\",";
    oss << "\"dropin_text\":\"" << json_escape(dropin) << "\",";
    oss << "\"unit_path\":\"" << json_escape(unit_path) << "\",";
    oss << "\"unit_text\":\"" << json_escape(unit) << "\"";
    oss << "}\n";
    return oss.str();
}

std::string WebServer::build_ui_html() const {
    static const char *html = R"HTML(
<!doctype html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Sentinel UI</title>
<style>
body{font-family:ui-monospace,Consolas,monospace;background:#000;color:#ddd;margin:0;padding:14px;}
.wrap{max-width:1100px;margin:0 auto;display:grid;gap:10px;}
.card{background:#000;border:1px solid #444;padding:10px;}
.title{font-size:14px;font-weight:700;margin-bottom:8px;color:#ddd;}
.stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;}
.kv{background:#000;border:1px solid #333;padding:8px;min-height:40px;}
.layout{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
.toolbar{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin-bottom:6px}
.logs{border:1px solid #333;background:#000;padding:8px;height:300px;overflow:auto;white-space:pre-wrap;line-height:1.35}
input{background:#000;color:#ddd;border:1px solid #444;padding:6px 8px;min-width:180px}
button{background:#000;color:#ddd;border:1px solid #555;padding:6px 9px;cursor:pointer}
button:hover{background:#111}
.hint{opacity:.85;font-size:12px}
@media (max-width:980px){.layout,.stats{grid-template-columns:1fr}}
</style></head><body><div class='wrap'>
<div class='card'>
  <div class='title'>Sentinel Overview</div>
  <div class='stats'>
    <div class='kv' id='time'>TIME</div><div class='kv' id='date'>DATE</div>
    <div class='kv' id='cpu'>CPU</div><div class='kv' id='temp'>TEMP</div>
    <div class='kv' id='mem'>MEM</div><div class='kv' id='net'>NET</div>
    <div class='kv' id='up'>UP</div><div class='kv' id='evt'>EVENT</div>
  </div>
</div>
<div class='layout'>
  <div class='card'>
    <div class='title'>Config Control</div>
    <div class='toolbar'>
      <input id='api_token' placeholder='API token (optional)'/>
      <button onclick='saveToken()'>Use Token</button>
    </div>
    <div class='toolbar'>
      <input id='cfg_key' placeholder='SENTINEL_WEB_ENABLE'/>
      <input id='cfg_val' placeholder='1'/>
      <button onclick='saveCfg()'>Save + Restart</button>
      <button onclick='loadCfg()'>Reload</button>
      <span id='cfg_status' class='hint'></span>
    </div>
    <div class='hint'>Parsed environment from override file:</div>
    <div class='logs' id='cfg_dump' style='height:130px'></div>
    <div class='hint'>Config files (systemd unit + override):</div>
    <div class='logs' id='cfg_files' style='height:220px'></div>
  </div>
  <div class='card'>
    <div class='title'>Terminal Stream</div>
    <div class='logs' id='logs'></div>
  </div>
</div>
<div class='card'>
  <div class='title'>Log File Tail</div>
  <div class='toolbar'>
    <input id='log_lines' value='200' style='min-width:90px'/>
    <button onclick='loadLogFile()'>Reload Log File</button>
    <span id='log_path' class='hint'></span>
  </div>
  <div class='logs' id='log_file_view'></div>
</div>
</div>
<script>
let TOKEN=localStorage.getItem('sentinel_token')||'';
function headers(){return TOKEN?{'X-Sentinel-Token':TOKEN}:{};}
function saveToken(){TOKEN=document.getElementById('api_token').value||'';localStorage.setItem('sentinel_token',TOKEN);}
function fmtObj(o){return Object.entries(o||{}).map(([k,v])=>k+'='+v).join('\n');}
async function getJson(url,opt={}){const r=await fetch(url,opt); if(!r.ok) throw new Error('http '+r.status); return r.json();}
async function loadCfg(){
  try{
    const c=await getJson('/api/config',{cache:'no-store',headers:headers()});
    document.getElementById('cfg_dump').textContent=fmtObj(c.env||{});
    const f=await getJson('/api/config/files',{cache:'no-store',headers:headers()});
    document.getElementById('cfg_files').textContent=
      '[UNIT] '+(f.unit_path||'')+'\n'+(f.unit_text||'(missing)')+'\n\n'+
      '[OVERRIDE] '+(f.dropin_path||'')+'\n'+(f.dropin_text||'(missing)');
  }catch(e){ document.getElementById('cfg_status').textContent='config read failed';}
}
async function saveCfg(){
  const k=document.getElementById('cfg_key').value.trim();
  const v=document.getElementById('cfg_val').value;
  if(!k) return;
  try{
    const h=headers(); h['Content-Type']='text/plain';
    const j=await getJson('/api/config',{method:'POST',headers:h,body:k+'='+v});
    document.getElementById('cfg_status').textContent=j.ok?'saved & restarted':'error: '+(j.error||'unknown');
    await loadCfg();
  }catch(e){ document.getElementById('cfg_status').textContent='save failed';}
}
async function loadLogFile(){
  try{
    const lines=parseInt(document.getElementById('log_lines').value||'200',10)||200;
    const q=Math.max(10,Math.min(2000,lines));
    const j=await getJson('/api/logfile?lines='+q,{cache:'no-store',headers:headers()});
    document.getElementById('log_path').textContent=j.path||'';
    document.getElementById('log_file_view').textContent=j.text||'';
  }catch(e){}
}
async function tick(){
  try{
    const s=await getJson('/api/state',{cache:'no-store',headers:headers()});
    const e=await getJson('/api/events?limit=160',{cache:'no-store',headers:headers()});
    document.getElementById('time').textContent='TIME  '+(s.time||'-');
    document.getElementById('date').textContent='DATE  '+(s.date||'-');
    document.getElementById('cpu').textContent='CPU   '+(s.cpu_pct??0).toFixed(1)+'%';
    document.getElementById('temp').textContent='TEMP  '+(s.cpu_temp_c??0).toFixed(1)+'C';
    document.getElementById('mem').textContent='MEM   '+(s.mem_used_mb||0)+' / '+(s.mem_total_mb||0)+' MB  '+(s.mem_pct||0)+'%';
    document.getElementById('net').textContent='NET   '+(s.ip==='NO_LINK'?'DOWN':'UP')+'  '+(s.iface||'-')+'  '+(s.ip||'-');
    document.getElementById('up').textContent='UP    '+(s.uptime||'-');
    document.getElementById('evt').textContent='EVENT '+(s.event||'-');
    document.getElementById('logs').textContent=(e.events||[]).join('\n');
  }catch(_){}
}
document.getElementById('api_token').value=TOKEN;
loadCfg(); loadLogFile(); tick();
setInterval(tick,1500); setInterval(loadLogFile,5000);
</script></body></html>
)HTML";
    return std::string(html);
}

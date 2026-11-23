#include "apps/ai/chat.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <utility>

extern char** environ;

namespace zs::ai {
namespace {

// ---- a small JSON reader: enough to walk the replies ----------------------

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json& operator[](const char* key) const {
        static const Json none;
        for (auto& f : fields)
            if (f.first == key) return f.second;
        return none;
    }
    const Json& at(size_t i) const {
        static const Json none;
        return i < items.size() ? items[i] : none;
    }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    bool parse(Json& out) {
        bool ok = value(out, 0);
        ws();
        return ok && i_ == s_.size();
    }

private:
    void ws() {
        while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_;
    }
    bool lit(const char* w) {
        size_t n = std::strlen(w);
        if (s_.compare(i_, n, w) != 0) return false;
        i_ += n;
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) {
            o += char(cp);
        } else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6));
            o += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xF0 | (cp >> 18));
            o += char(0x80 | ((cp >> 12) & 0x3F));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char ch = s_[i_++];
            if (!std::isxdigit((unsigned char)ch)) return false;
            v = v * 16 + unsigned(std::isdigit((unsigned char)ch) ? ch - '0' : std::tolower(ch) - 'a' + 10);
        }
        return true;
    }
    bool string(std::string& o) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            char ch = s_[i_++];
            if (ch == '"') return true;
            if (ch != '\\') {
                o += ch;
                continue;
            }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        size_t back = i_;
                        unsigned lo;
                        i_ += 2;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            i_ = back;
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xD800 && cp < 0xE000) {
                        cp = 0xFFFD;
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += e;
            }
        }
        return false;
    }
    bool value(Json& out, int depth) {
        if (depth > 48) return false;
        ws();
        if (i_ >= s_.size()) return false;
        char ch = s_[i_];
        if (ch == '{') {
            out.type = Json::Object;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return true;
            }
            while (true) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.fields.emplace_back(std::move(key), std::move(v));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (ch == '[') {
            out.type = Json::Array;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return true;
            }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.items.push_back(std::move(v));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (ch == '"') {
            out.type = Json::String;
            return string(out.str);
        }
        if (lit("true")) { out.type = Json::Bool; out.b = true; return true; }
        if (lit("false")) { out.type = Json::Bool; return true; }
        if (lit("null")) return true;
        char* end = nullptr;
        out.num = std::strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) return false;
        out.type = Json::Number;
        i_ = size_t(end - s_.c_str());
        return true;
    }

    const std::string& s_;
    size_t i_ = 0;
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool starts_with(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

// Values are one line: newlines and backslashes are escaped.
std::string esc(const std::string& v) {
    std::string o;
    for (char ch : v) {
        if (ch == '\\') o += "\\\\";
        else if (ch == '\n') o += "\\n";
        else if (ch != '\r') o += ch;
    }
    return o;
}
std::string unesc(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            ++i;
            o += v[i] == 'n' ? '\n' : v[i];
        } else {
            o += v[i];
        }
    }
    return o;
}

using Conf = std::map<std::string, std::string>;

Conf read_conf(const std::string& path) {
    Conf c;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        c[line.substr(0, eq)] = unesc(line.substr(eq + 1));
    }
    return c;
}

std::string get(const Conf& c, const std::string& k) {
    auto it = c.find(k);
    return it == c.end() ? "" : it->second;
}

// A private file (0600) holding data, for curl to read. "" on failure.
std::string private_file(const std::string& data) {
    const char* dir = std::getenv("TMPDIR");
    std::string tmpl = std::string(dir && *dir ? dir : "/tmp") + "/zeroos-ai-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd < 0) return "";
    size_t done = 0;
    while (done < data.size()) {
        ssize_t n = write(fd, data.data() + done, data.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            close(fd);
            unlink(buf.data());
            return "";
        }
        done += size_t(n);
    }
    close(fd);
    return buf.data();
}

// Claude models that take server-side refusal fallbacks ("fallbacks":"default").
bool takes_fallbacks(const std::string& model) {
    return starts_with(model, "claude-opus-5-5") || starts_with(model, "claude-sonnet-5-5") ||
           starts_with(model, "claude-fable-5-");
}

std::string error_message(const Json& j) {
    const Json& root = j.type == Json::Array ? j.at(0) : j;   // Gemini wraps errors in a list
    const Json& e = root["error"];
    if (e.type == Json::String) return e.str;
    if (e["message"].type == Json::String) return e["message"].str;
    if (root["message"].type == Json::String) return root["message"].str;
    if (root["detail"].type == Json::String) return root["detail"].str;
    return "";
}

std::string shorten(std::string s, size_t n) {
    if (s.size() > n) s = s.substr(0, n) + "...";
    return s;
}

}  // namespace

const std::vector<Provider>& providers() {
    static const std::vector<Provider> list = {
        {"anthropic", "Claude (Anthropic)", Wire::Anthropic, "https://api.anthropic.com/v1/messages",
         "claude-opus-5-5", true, "console.anthropic.com > API keys"},
        {"openai", "ChatGPT (OpenAI)", Wire::OpenAI, "https://api.openai.com/v1/chat/completions", "gpt-5-mini",
         true, "platform.openai.com > API keys"},
        {"gemini", "Gemini (Google)", Wire::OpenAI,
         "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions", "gemini-2.5-flash", true,
         "aistudio.google.com > Get API key"},
        {"openrouter", "OpenRouter", Wire::OpenAI, "https://openrouter.ai/api/v1/chat/completions",
         "openrouter/auto", true, "openrouter.ai > Keys"},
        {"groq", "Groq", Wire::OpenAI, "https://api.groq.com/openai/v1/chat/completions",
         "llama-3.3-70b-versatile", true, "console.groq.com > API Keys"},
        {"mistral", "Mistral", Wire::OpenAI, "https://api.mistral.ai/v1/chat/completions", "mistral-small-latest",
         true, "console.mistral.ai > API keys"},
        {"ollama", "Ollama (your PC)", Wire::OpenAI, "http://localhost:11434/v1/chat/completions", "llama3.2",
         false, "No key. Set Server to your PC's address"},
        {"custom", "Other (OpenAI-compatible)", Wire::OpenAI, "", "", false,
         "LM Studio, llama.cpp, vLLM, LocalAI..."},
    };
    return list;
}

const Provider* find_provider(const std::string& id) {
    for (auto& p : providers())
        if (id == p.id) return &p;
    return nullptr;
}

const char* default_system_prompt() {
    return "You are a helpful AI assistant on a pocket computer (a CardputerZero running ZeroOS) with a "
           "320x170 screen and a small keyboard. Keep answers short and plain: a few sentences, no tables, "
           "little markdown. Give longer, step-by-step answers only when asked.";
}

std::string effective_model(const Settings& s) {
    if (!trim(s.model).empty()) return trim(s.model);
    const Provider* p = find_provider(s.provider);
    return p ? p->model : "";
}

std::string effective_url(const Settings& s) {
    if (!trim(s.url).empty()) return normalize_url(s.provider, s.url);
    const Provider* p = find_provider(s.provider);
    return p ? p->url : "";
}

std::string normalize_url(const std::string& provider_id, const std::string& typed) {
    std::string s;
    for (char ch : typed)
        if (!std::isspace((unsigned char)ch)) s += ch;
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty()) return s;
    const Provider* p = find_provider(provider_id);
    if (p && p->wire == Wire::Anthropic) return s.find("://") == std::string::npos ? "https://" + s : s;
    if (s.find("://") == std::string::npos) {
        auto slash = s.find('/');
        std::string host = s.substr(0, slash);
        bool has_port = host.rfind(':') != std::string::npos && host.back() != ']';
        if (!has_port && provider_id == "ollama") s.insert(host.size(), ":11434");
        s = "http://" + s;
    }
    if (ends_with(s, "/chat/completions")) return s;
    size_t path = s.find('/', s.find("://") + 3);
    if (path == std::string::npos) return s + "/v1/chat/completions";
    return s + "/chat/completions";
}

Settings load(const std::string& path) {
    Conf c = read_conf(path);
    Settings s;
    std::string p = get(c, "provider");
    if (find_provider(p)) s.provider = p;
    s.model = get(c, s.provider + ".model");
    s.key = get(c, s.provider + ".key");
    s.url = get(c, s.provider + ".url");
    s.system = get(c, "system");
    return s;
}

bool save(const std::string& path, const Settings& s) {
    Conf c = read_conf(path);   // keep the other providers' keys
    c["provider"] = s.provider;
    c[s.provider + ".model"] = s.model;
    c[s.provider + ".key"] = s.key;
    c[s.provider + ".url"] = s.url;
    c["system"] = s.system;
    std::string out = "# AI settings. Holds API keys: keep it private (mode 600).\n";
    for (auto& kv : c)
        if (!kv.second.empty()) out += kv.first + "=" + esc(kv.second) + "\n";

    // ~/.config/zeroos may not exist yet.
    for (size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1))
        mkdir(path.substr(0, at).c_str(), 0700);
    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    fchmod(fd, 0600);
    bool ok = write(fd, out.data(), out.size()) == ssize_t(out.size()) && fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

void switch_provider(const std::string& path, Settings& s, const std::string& provider_id) {
    if (!find_provider(provider_id)) return;
    Conf c = read_conf(path);
    s.provider = provider_id;
    s.model = get(c, provider_id + ".model");
    s.key = get(c, provider_id + ".key");
    s.url = get(c, provider_id + ".url");
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') {
            o += '\\';
            o += char(ch);
        } else if (ch == '\n') {
            o += "\\n";
        } else if (ch < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", ch);
            o += b;
        } else {
            o += char(ch);
        }
    }
    return o;
}

Request build_request(const Settings& s, const std::vector<Message>& history) {
    const Provider* p = find_provider(s.provider);
    Request r;
    r.url = effective_url(s);
    std::string model = effective_model(s);
    std::string system = trim(s.system).empty() ? default_system_prompt() : s.system;
    std::string key = trim(s.key);
    r.headers.push_back("Content-Type: application/json");

    // The newest turns only: a long chat would get slow and costly.
    size_t first = history.size() > 40 ? history.size() - 40 : 0;
    while (first < history.size() && !history[first].user) ++first;   // start on a user turn

    std::string msgs;
    for (size_t i = first; i < history.size(); ++i) {
        if (!msgs.empty()) msgs += ",";
        msgs += std::string("{\"role\":\"") + (history[i].user ? "user" : "assistant") + "\",\"content\":\"" +
                json_escape(history[i].text) + "\"}";
    }

    if (p && p->wire == Wire::Anthropic) {
        r.headers.push_back("x-api-key: " + key);
        r.headers.push_back("anthropic-version: 2026-10-21");
        std::string extra;
        if (takes_fallbacks(model)) {
            // If Claude's safety checks decline, the server retries on a
            // suitable model instead of returning nothing.
            r.headers.push_back("anthropic-beta: server-side-fallback-2026-07-01");
            extra = ",\"fallbacks\":\"default\"";
        }
        r.body = "{\"model\":\"" + json_escape(model) + "\",\"max_tokens\":16000,\"system\":\"" +
                 json_escape(system) + "\",\"messages\":[" + msgs + "]" + extra + "}";
    } else {
        if (!key.empty()) r.headers.push_back("Authorization: Bearer " + key);
        if (s.provider == "openrouter") {
            r.headers.push_back("X-Title: AI for ZeroOS");
        }
        std::string sys = "{\"role\":\"system\",\"content\":\"" + json_escape(system) + "\"}";
        r.body = "{\"model\":\"" + json_escape(model) + "\",\"messages\":[" + sys + (msgs.empty() ? "" : ",") +
                 msgs + "]}";
    }
    return r;
}

bool parse_reply(Wire wire, int http_status, const std::string& body, std::string& text, std::string& error) {
    text.clear();
    error.clear();
    Json j;
    bool parsed = Parser(body).parse(j);

    if (http_status == 0) {
        error = "No answer from the server. Connected to the internet?";
        return false;
    }
    if (http_status < 200 || http_status >= 300) {
        std::string msg = parsed ? error_message(j) : "";
        std::string what = http_status == 401 || http_status == 403 ? "Key not accepted"
                           : http_status == 404                     ? "Not found (check the model or server)"
                           : http_status == 429                     ? "Too many requests or out of credit"
                           : http_status == 529 || http_status == 503 ? "Service busy, try again"
                           : http_status >= 500                     ? "Server error"
                                                                    : "Request refused";
        error = what + std::string(" (") + std::to_string(http_status) + ")";
        if (!msg.empty()) error += ": " + shorten(msg, 160);
        return false;
    }
    if (!parsed) {
        error = "The reply wasn't understood (is the server address right?)";
        return false;
    }

    if (wire == Wire::Anthropic) {
        for (const Json& b : j["content"].items)
            if (b["type"].str == "text") text += b["text"].str;
        const std::string& stop = j["stop_reason"].str;
        if (stop == "refusal") {
            if (text.empty()) {
                error = "Claude declined to answer that.";
                return false;
            }
        } else if (stop == "max_tokens") {
            text += "\n[cut off: the answer was too long]";
        }
    } else {
        const Json& choice = j["choices"].at(0);
        const Json& msg = choice["message"];
        if (msg["content"].type == Json::String) {
            text = msg["content"].str;
        } else {
            for (const Json& b : msg["content"].items)   // some servers send parts
                if (b["text"].type == Json::String) text += b["text"].str;
        }
        if (text.empty() && msg["refusal"].type == Json::String) {
            error = "Declined: " + shorten(msg["refusal"].str, 160);
            return false;
        }
        if (choice["finish_reason"].str == "length") text += "\n[cut off: the answer was too long]";
    }
    text = trim(text);
    if (text.empty()) {
        std::string msg = error_message(j);
        error = msg.empty() ? "Empty answer" : shorten(msg, 160);
        return false;
    }
    return true;
}

bool chat(const Settings& s, const std::vector<Message>& history, std::string& reply, std::string& error,
          std::atomic<int>* cancel_pid) {
    reply.clear();
    error.clear();
    if (const char* fake = std::getenv("ZEROOS_AI_FAKE")) {
        // Screenshots and tests: no network, a canned answer.
        reply = *fake ? fake : "Hi! I'm your AI assistant. Ask me anything.";
        return true;
    }
    const Provider* p = find_provider(s.provider);
    if (!p) {
        error = "Pick a provider in Settings (Tab)";
        return false;
    }
    if (p->needs_key && trim(s.key).empty()) {
        error = "Add your " + std::string(p->name) + " API key in Settings (Tab)";
        return false;
    }
    if (effective_model(s).empty()) {
        error = "Set a model in Settings (Tab)";
        return false;
    }
    Request r = build_request(s, history);
    if (r.url.empty()) {
        error = "Set the server address in Settings (Tab)";
        return false;
    }

#ifdef __EMSCRIPTEN__
    error = "The browser demo can't reach AI services";
    return false;
#else
    std::string headers;
    for (auto& h : r.headers) headers += h + "\n";
    std::string hfile = private_file(headers), bfile = private_file(r.body);
    if (hfile.empty() || bfile.empty()) {
        if (!hfile.empty()) unlink(hfile.c_str());
        if (!bfile.empty()) unlink(bfile.c_str());
        error = "Couldn't write a temporary file (card full?)";
        return false;
    }

    std::string at_h = "@" + hfile, at_b = "@" + bfile;
    const char* argv[] = {"curl", "-sS", "-L", "--max-time", "120", "--connect-timeout", "10",
                          "-H", at_h.c_str(), "--data-binary", at_b.c_str(),
                          "-w", "\n%{http_code}", r.url.c_str(), nullptr};

    int out[2];
    if (pipe2(out, O_CLOEXEC) != 0) {
        unlink(hfile.c_str());
        unlink(bfile.c_str());
        error = "Couldn't start curl";
        return false;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, "curl", &fa, nullptr, const_cast<char* const*>(argv), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(out[1]);
    std::string body;
    if (rc == 0) {
        if (cancel_pid) cancel_pid->store(pid);
        char buf[4096];
        ssize_t n;
        while ((n = read(out[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
            if (n > 0) body.append(buf, size_t(n));
        int st = 0;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        if (cancel_pid) cancel_pid->store(0);
    }
    close(out[0]);
    unlink(hfile.c_str());
    unlink(bfile.c_str());
    if (rc != 0) {
        error = "curl is missing: reinstall the AI app in Apps";
        return false;
    }

    int status = 0;
    auto nl = body.rfind('\n');
    if (nl != std::string::npos) {
        status = std::atoi(body.c_str() + nl + 1);
        body.erase(nl);
    }
    return parse_reply(p->wire, status, body, reply, error);
#endif
}

}  // namespace zs::ai

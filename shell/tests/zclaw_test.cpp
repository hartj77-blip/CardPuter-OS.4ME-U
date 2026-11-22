// ZClaw's AI layer without a network: requests, replies, settings, and a
// whole chat() through a stand-in curl. cmake ... && ctest, or run zclaw_test.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "apps/zclaw/ai.h"

using namespace zs::zclaw;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    std::vector<Message> h = {{true, "Hi \"there\"\nline two"}, {false, "Hello!"}, {true, "What's 2+2?"}};

    // Claude: Messages API, key in x-api-key, refusal fallbacks on current models.
    {
        Settings s;
        s.provider = "anthropic";
        s.key = "sk-ant-test";
        Request r = build_request(s, h);
        CHECK(r.url == "https://api.anthropic.com/v1/messages");
        CHECK(has(r.body, "\"model\":\"claude-opus-5-5\""));
        CHECK(has(r.body, "\"max_tokens\":16000"));
        CHECK(has(r.body, "\"system\":\"You are ZClaw"));
        CHECK(has(r.body, "Hi \\\"there\\\"\\nline two"));
        CHECK(has(r.body, "\"role\":\"assistant\",\"content\":\"Hello!\""));
        CHECK(has(r.body, "\"fallbacks\":\"default\""));
        bool key = false, ver = false;
        for (auto& x : r.headers) {
            key |= x == "x-api-key: sk-ant-test";
            ver |= x == "anthropic-version: 2026-10-21";
        }
        CHECK(key && ver);
        s.model = "claude-haiku-5-5";   // no server-side fallback for Haiku
        r = build_request(s, h);
        CHECK(!has(r.body, "fallbacks"));
    }

    // OpenAI-compatible: system message first, Bearer key, no key for Ollama.
    {
        Settings s;
        s.provider = "gemini";
        s.key = "AIza-test";
        Request r = build_request(s, h);
        CHECK(has(r.url, "generativelanguage.googleapis.com"));
        CHECK(has(r.body, "\"messages\":[{\"role\":\"system\""));
        CHECK(has(r.body, "\"model\":\"gemini-2.5-flash\""));
        bool bearer = false;
        for (auto& x : r.headers) bearer |= x == "Authorization: Bearer AIza-test";
        CHECK(bearer);

        s.provider = "ollama";
        s.key = "";
        s.url = "192.168.1.20";
        r = build_request(s, h);
        CHECK(r.url == "http://192.168.1.20:11434/v1/chat/completions");
        for (auto& x : r.headers) CHECK(!has(x, "Authorization"));
    }

    // Typed server addresses.
    CHECK(normalize_url("custom", "pc.local:1234") == "http://pc.local:1234/v1/chat/completions");
    CHECK(normalize_url("custom", "http://pc:8080/v1/") == "http://pc:8080/v1/chat/completions");
    CHECK(normalize_url("custom", "https://x.ai/api/v1/chat/completions") == "https://x.ai/api/v1/chat/completions");
    CHECK(normalize_url("ollama", "") == "");

    // Replies.
    {
        std::string t, e;
        CHECK(parse_reply(Wire::Anthropic, 200,
                          R"({"content":[{"type":"thinking","thinking":""},{"type":"text","text":" 4 é"}],)"
                          R"("stop_reason":"end_turn"})",
                          t, e));
        CHECK(t == "4 \xc3\xa9");
        CHECK(!parse_reply(Wire::Anthropic, 200, R"({"content":[],"stop_reason":"refusal"})", t, e));
        CHECK(has(e, "declined"));
        CHECK(!parse_reply(Wire::Anthropic, 401,
                           R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})",
                           t, e));
        CHECK(has(e, "Key not accepted") && has(e, "invalid x-api-key"));
        CHECK(parse_reply(Wire::OpenAI, 200,
                          R"({"choices":[{"message":{"role":"assistant","content":"Four."},"finish_reason":"length"}]})",
                          t, e));
        CHECK(has(t, "Four.") && has(t, "cut off"));
        CHECK(!parse_reply(Wire::OpenAI, 400, R"([{"error":{"code":400,"message":"API key not valid"}}])", t, e));
        CHECK(has(e, "API key not valid"));
        CHECK(!parse_reply(Wire::OpenAI, 0, "", t, e));
        CHECK(has(e, "internet"));
        CHECK(!parse_reply(Wire::OpenAI, 200, "<html>", t, e));
    }

    char dir[] = "/tmp/zclaw_test_XXXXXX";
    if (!mkdtemp(dir)) return 1;
    std::string d = dir;

    // Settings: private file, one key per provider, newlines survive.
    {
        std::string path = d + "/cfg/zeroos/zclaw.conf";
        Settings s;
        s.provider = "anthropic";
        s.key = "sk-ant-1";
        s.system = "Be brief.\nUse metric.";
        CHECK(save(path, s));
        struct stat st;
        CHECK(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
        switch_provider(path, s, "openai");
        CHECK(s.provider == "openai" && s.key.empty());
        s.key = "sk-oai";
        s.model = "gpt-5";
        CHECK(save(path, s));
        Settings back = load(path);
        CHECK(back.provider == "openai" && back.key == "sk-oai" && back.model == "gpt-5");
        CHECK(back.system == "Be brief.\nUse metric.");
        switch_provider(path, back, "anthropic");
        CHECK(back.key == "sk-ant-1" && back.model.empty());
    }

    // A whole chat through a stand-in curl, which checks the key arrives in
    // the header file and is not on its command line.
    {
        std::string bin = d + "/bin";
        mkdir(bin.c_str(), 0755);
        std::ofstream(bin + "/curl") << "#!/bin/sh\n"
                                        "echo \"$@\" > " << d << "/args\n"
                                        "for a; do case $a in @*) cat \"${a#@}\" >> " << d << "/files;; esac; done\n"
                                        "printf '{\"content\":[{\"type\":\"text\",\"text\":\"Four.\"}],"
                                        "\"stop_reason\":\"end_turn\"}\\n200'\n";
        chmod((bin + "/curl").c_str(), 0755);
        std::string old = std::getenv("PATH") ? std::getenv("PATH") : "";
        setenv("PATH", (bin + ":" + old).c_str(), 1);
        unsetenv("ZEROOS_ZCLAW_FAKE");

        Settings s;
        s.key = "sk-ant-secret";
        std::string reply, error;
        std::atomic<int> pid{0};
        CHECK(chat(s, h, reply, error, &pid));
        CHECK(reply == "Four.");
        CHECK(pid.load() == 0);
        CHECK(!has(slurp(d + "/args"), "sk-ant-secret"));
        CHECK(has(slurp(d + "/files"), "x-api-key: sk-ant-secret"));
        CHECK(has(slurp(d + "/files"), "\"What's 2+2?\""));

        s.key = "";
        CHECK(!chat(s, h, reply, error));
        CHECK(has(error, "API key"));
        setenv("PATH", old.c_str(), 1);
    }

    if (std::system(("rm -rf '" + d + "'").c_str())) {}
    if (failures) {
        std::printf("zclaw_test: %d failed\n", failures);
        return 1;
    }
    std::printf("zclaw_test: all passed\n");
    return 0;
}

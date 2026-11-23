// The AI app's chat layer: one chat request to whichever service the user picked.
// No LVGL here, so it can be unit-tested (tests/ai_test.cpp).
//
// Two wire formats cover every provider:
//   Anthropic   POST /v1/messages          (Claude)
//   OpenAI      POST .../chat/completions  (OpenAI, Gemini, OpenRouter, Groq,
//                                           Mistral, Ollama, LM Studio, any
//                                           OpenAI-compatible server)
// HTTP goes through the curl command, as the rest of ZeroOS does. The API key
// travels in a private temporary file, never on a command line.
#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace zs::ai {

enum class Wire { Anthropic, OpenAI };

struct Provider {
    const char* id;          // settings key: "anthropic", "openai", ...
    const char* name;        // shown to people
    Wire wire;
    const char* url;         // full endpoint; "" = the user types one
    const char* model;       // a sensible default; the user can change it
    bool needs_key;
    const char* key_hint;    // where to get a key
};

// Every provider the AI app knows, in the order the settings page lists them.
const std::vector<Provider>& providers();
const Provider* find_provider(const std::string& id);

struct Settings {
    std::string provider = "anthropic";
    std::string model;       // "" = the provider's default
    std::string key;
    std::string url;         // "" = the provider's default
    std::string system;      // system prompt; "" = the built-in one
};

// The one in use, with defaults filled in.
std::string effective_model(const Settings& s);
std::string effective_url(const Settings& s);
const char* default_system_prompt();

// ~/.config/zeroos/ai.conf (mode 0600: it holds API keys). Each provider
// keeps its own key, model and URL, so switching back and forth loses nothing.
Settings load(const std::string& path);
bool save(const std::string& path, const Settings& s);
// Fills s.model/key/url from what was saved for another provider.
void switch_provider(const std::string& path, Settings& s, const std::string& provider_id);

struct Message {
    bool user;               // false: the assistant
    std::string text;
};

struct Request {
    std::string url;
    std::vector<std::string> headers;   // "Name: value"
    std::string body;                   // JSON
};

// The HTTP request for one turn. history ends with the user's new message.
Request build_request(const Settings& s, const std::vector<Message>& history);

// The reply's text, or false and a short sentence a person can act on.
bool parse_reply(Wire wire, int http_status, const std::string& body, std::string& text, std::string& error);

// Runs the request with curl. Blocks (up to about two minutes): call it off
// the UI thread. cancel_pid, if given, receives curl's pid so another thread
// can kill() it.
bool chat(const Settings& s, const std::vector<Message>& history, std::string& reply, std::string& error,
          std::atomic<int>* cancel_pid = nullptr);

// A server as typed ("192.168.1.20", "pc.local:1234", "http://host/v1") made
// into a full chat endpoint. Bare hosts get http:// and, for Ollama, its port
// 11434; a path without /chat/completions gets it (and /v1 when it has none).
std::string normalize_url(const std::string& provider_id, const std::string& typed);

// JSON string escaping, exposed for the tests.
std::string json_escape(const std::string& s);

}  // namespace zs::ai

# ZClaw

ZClaw is an AI assistant for the Cardputer Zero. Type a question and get an answer on the
built-in screen. It works with Claude and with other AI services, so you can use whichever one you
already pay for, or run a model on your own PC for free.

Install it from **Apps**. It needs an internet connection (Wi-Fi or a tethered phone), or only your
home network if you use Ollama on your own PC.

## Providers

| Provider | Default model | Where to get a key |
| --- | --- | --- |
| Claude (Anthropic) | `claude-opus-5-5` | console.anthropic.com > API keys |
| ChatGPT (OpenAI) | `gpt-5-mini` | platform.openai.com > API keys |
| Gemini (Google) | `gemini-2.5-flash` | aistudio.google.com > Get API key (free tier) |
| OpenRouter | `openrouter/auto` | openrouter.ai > Keys (one key, hundreds of models) |
| Groq | `llama-3.3-70b-versatile` | console.groq.com > API Keys (free tier) |
| Mistral | `mistral-small-latest` | console.mistral.ai > API keys |
| Ollama (your PC) | `llama3.2` | No key needed |
| Other (OpenAI-compatible) | you choose | LM Studio, llama.cpp server, vLLM, LocalAI... |

You can change the model for any provider. For example, set Claude to `claude-haiku-5-5` for quicker,
cheaper answers or to `claude-sonnet-5-5` for a balance of the two. Each provider keeps its own key,
model and server, so switching between them doesn't lose your settings.

## Getting started

1. Open **ZClaw**. The first time, it opens on its settings page.
2. **Provider**: pick a service.
3. **API key**: type the key. A key is long, so it's easier to send it over SSH (see below).
4. Press **Tab** to go to the chat page, type a question and press **Enter**.

| Key | |
| --- | --- |
| Enter | send the message |
| Up / Down | scroll the conversation |
| Tab | settings / back to the chat |
| Esc | while it's thinking: stop. In settings: back to the chat. Otherwise: leave the app |

If a request fails, the reason is shown (for example, a key that wasn't accepted or no credit
left). Press Enter on the empty box to try the question again.

**Settings** also has:

- **New chat**: starts over. The conversation stays while ZeroShell runs, even after you leave
  the app, and is never written to the card unless you save it.
- **Save chat to Notes**: writes the conversation to `~/Notes/ZClaw <date>.txt`. Open it in Notes,
  or sync it to your PC with Sync.
- **Personality**: the system prompt. The default one asks for short, plain answers that fit the
  small screen.

## Running a model on your own PC (Ollama)

Nothing leaves your home network, and there's no key or bill.

1. Install [Ollama](https://ollama.com) on your PC and download a model: `ollama pull llama3.2`.
2. Let other devices connect to it: set `OLLAMA_HOST=0.0.0.0` on the PC and restart Ollama.
3. In ZClaw, pick **Ollama (your PC)** and set **Server** to the PC's address, such as
   `192.168.1.20` or `my-pc.local`. ZClaw adds the `http://`, the port (11434) and the path.

LM Studio and other servers work the same way with **Other (OpenAI-compatible)**. Type the address
and port, for example `192.168.1.20:1234`.

## Setting a key from your PC

Typing a long key on the small keyboard is slow. Over SSH (`ssh pi@cardputer.local`), edit
`~/.config/zeroos/zclaw.conf` and add a line such as:

```
anthropic.key=sk-ant-...
```

The file has one `provider.key`, `provider.model` and `provider.url` line per provider (for
example `openai.key`), plus `provider=` for the one in use. Reopen ZClaw to load the changes.

## Privacy and cost

- Your key is stored on the card in `~/.config/zeroos/zclaw.conf`. Only your user can read the file
  (mode 600). While a request is being sent, the key is kept in a private temporary file, so it
  never appears in the process list.
- Each question is sent to the provider you chose, with the recent conversation (up to the last 40
  messages), and the provider bills your account for it. Ollama keeps everything on your network.
- ZClaw is a chat client. It doesn't run commands or change anything on the device.

## How it works

`shell/src/apps/zclaw/ai.cpp` builds the request. It uses the Anthropic Messages API for Claude and
the OpenAI chat-completions format for every other provider. Gemini, OpenRouter, Groq, Mistral and
Ollama all offer the OpenAI format. The request is sent with `curl` on a background thread, so the
screen stays responsive, and leaving the app stops it. On current Claude models, ZClaw turns on
Anthropic's server-side fallback, so a request that Claude's safety checks decline is retried on
another suitable model. `shell/tests/zclaw_test.cpp` tests the requests, replies and settings
without a network.

The name is a nod to the "claw" family of pocket AI assistants that M5Stack's community has built
for the Cardputer. ZClaw is a separate app written for ZeroOS.

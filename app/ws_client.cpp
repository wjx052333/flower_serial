/**
 * Interactive WebSocket test client for serial_ws server.
 *
 * Usage: ws_client [host] [port]
 *   default: ws://127.0.0.1:8765
 *
 * Commands at the prompt:
 *   o  -> relay_open
 *   c  -> relay_close
 *   r  -> relay_query
 *   s  -> soil_query
 *   q  -> quit
 */

#include <rtc/rtc.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <unistd.h>

static std::mutex              g_mtx;
static std::condition_variable g_cv;
static std::atomic<bool>       g_open{false};
static std::atomic<bool>       g_closed{false};
static std::string             g_reply;
static bool                    g_reply_ready{false};

static void wait_reply() {
    std::unique_lock<std::mutex> lk(g_mtx);
    g_cv.wait(lk, [] { return g_reply_ready || g_closed.load(); });
    if (g_reply_ready) {
        std::printf("<< %s\n", g_reply.c_str());
        g_reply_ready = false;
    }
}

int main(int argc, char *argv[]) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char *port = (argc > 2) ? argv[2] : "8765";

    std::string url = std::string("ws://") + host + ":" + port;
    std::printf("connecting to %s ...\n", url.c_str());

    rtc::WebSocket ws;

    ws.onOpen([&]() {
        std::printf("connected.\n");
        std::printf("commands:  o=relay_open  c=relay_close  r=relay_query  t=relay_open_timed(180s)  s=soil_query  q=quit\n");
        g_open = true;
        g_cv.notify_all();
    });

    ws.onClosed([&]() {
        std::printf("connection closed.\n");
        g_closed = true;
        g_cv.notify_all();
    });

    ws.onError([](const std::string &err) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
    });

    ws.onMessage([&](rtc::message_variant msg) {
        if (!std::holds_alternative<std::string>(msg))
            return;
        std::lock_guard<std::mutex> lk(g_mtx);
        g_reply = std::get<std::string>(msg);
        g_reply_ready = true;
        g_cv.notify_all();
    });

    ws.open(url);

    /* Wait for open or failure */
    {
        std::unique_lock<std::mutex> lk(g_mtx);
        g_cv.wait(lk, [&] { return g_open.load() || g_closed.load(); });
    }
    if (!g_open) return 1;

    static const struct { char key; const char *cmd; } table[] = {
        { 'o', R"({"cmd":"relay_open"})"                   },
        { 'c', R"({"cmd":"relay_close"})"                  },
        { 'r', R"({"cmd":"relay_query"})"                  },
        { 't', R"({"cmd":"relay_open_timed","duration":180})" },
        { 's', R"({"cmd":"soil_query"})"                   },
    };

    char line[16];
    while (!g_closed && std::fgets(line, sizeof(line), stdin)) {
        char key = line[0];
        if (key == 'q') break;

        const char *payload = nullptr;
        for (auto &e : table)
            if (e.key == key) { payload = e.cmd; break; }

        if (!payload) {
            std::printf("unknown command '%c'\n", key);
            continue;
        }

        std::printf(">> %s\n", payload);
        ws.send(std::string(payload));
        wait_reply();
    }

    ws.close();
    /* Wait for close confirmation */
    {
        std::unique_lock<std::mutex> lk(g_mtx);
        g_cv.wait_for(lk, std::chrono::seconds(3), [] { return g_closed.load(); });
    }
    return 0;
}

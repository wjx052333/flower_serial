/**
 * WebSocket server for serial device control.
 *
 * JSON request/response protocol:
 *
 *   Request:  { "cmd": "<command>" [, "duration": <seconds>] }
 *
 *   Commands:
 *     "relay_open"        - open relay
 *     "relay_close"       - close relay
 *     "relay_query"       - query relay state
 *     "relay_open_timed"  - open relay for <duration> seconds, then auto-close
 *                           if relay is already open: force-close it and return error
 *     "soil_query"        - read soil sensor
 *
 *   Response (success):
 *     relay_open/close:      { "ok": true }
 *     relay_query:           { "ok": true, "state": "open" | "closed" | "unknown" }
 *     relay_open_timed:      { "ok": true, "duration": <seconds> }
 *     soil_query:            { "ok": true, "moisture": f, "temperature": f,
 *                              "conductivity": f, "ph": f,
 *                              "nitrogen": u, "phosphorus": u,
 *                              "potassium": u, "salinity": u }
 *
 *   Response (device absent or failed):
 *     { "ok": false, "error": "device unavailable" }
 */

#include <rtc/rtc.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

extern "C" {
#include "device.h"
#include "relay.h"
#include "soil.h"
}

static constexpr uint16_t WS_PORT = 8765;

/* ------------------------------------------------------------------ */
/* Minimal JSON helpers                                                */
/* ------------------------------------------------------------------ */

static std::string json_get_string(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    auto kpos = json.find(needle);
    if (kpos == std::string::npos) return {};
    auto colon = json.find(':', kpos + needle.size());
    if (colon == std::string::npos) return {};
    auto vstart = json.find('"', colon + 1);
    if (vstart == std::string::npos) return {};
    auto vend = json.find('"', vstart + 1);
    if (vend == std::string::npos) return {};
    return json.substr(vstart + 1, vend - vstart - 1);
}

/* Parse an integer field; returns def if not found or not a number. */
static int json_get_int(const std::string &json, const std::string &key, int def = -1) {
    std::string needle = "\"" + key + "\"";
    auto kpos = json.find(needle);
    if (kpos == std::string::npos) return def;
    auto colon = json.find(':', kpos + needle.size());
    if (colon == std::string::npos) return def;
    auto vstart = colon + 1;
    while (vstart < json.size() && (json[vstart] == ' ' || json[vstart] == '\t')) ++vstart;
    if (vstart >= json.size() || (json[vstart] != '-' && !std::isdigit((unsigned char)json[vstart])))
        return def;
    try {
        return std::stoi(json.substr(vstart));
    } catch (...) {
        return def;
    }
}

/* ------------------------------------------------------------------ */
/* Timestamp                                                           */
/* ------------------------------------------------------------------ */

static std::string now_str() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    char ms[8];
    std::snprintf(ms, sizeof(ms), ".%03d", (int)(ts.tv_nsec / 1000000));
    return std::string(buf) + ms;
}

static std::string json_ok()                 { return R"({"ok":true})"; }
static std::string json_unavailable()        { return R"({"ok":false,"error":"device unavailable"})"; }
static std::string json_err(const char *msg) { return std::string(R"({"ok":false,"error":")") + msg + "\"}"; }

static std::string json_relay_state(relay_state_t s) {
    const char *sv = (s == RELAY_ON) ? "open" : (s == RELAY_OFF) ? "closed" : "unknown";
    return std::string(R"({"ok":true,"state":")") + sv + "\"}";
}

static std::string json_ok_timed(int seconds) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), R"({"ok":true,"duration":%d})", seconds);
    return buf;
}

static std::string json_soil(const soil_data_t &d) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        R"({"ok":true,"moisture":%.1f,"temperature":%.1f,"conductivity":%.0f,"ph":%.1f,)"
        R"("nitrogen":%u,"phosphorus":%u,"potassium":%u,"salinity":%u})",
        (double)d.moisture, (double)d.temperature, (double)d.conductivity, (double)d.ph,
        d.nitrogen, d.phosphorus, d.potassium, d.salinity);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Per-device state                                                    */
/* ------------------------------------------------------------------ */

struct RelayDev {
    int        fd{-1};
    std::mutex mtx;

    void try_reconnect() { relay_device_reinit(&fd); }
};

struct SoilDev {
    int        fd{-1};
    std::mutex mtx;

    void try_reconnect(int relay_fd) { soil_device_reinit(&fd, relay_fd); }
};

static RelayDev          g_relay;
static SoilDev           g_soil;
static std::atomic<bool> g_running{true};

/*
 * Timed-open state.
 *
 * g_relay_is_open  – true while relay is open (set/cleared under g_relay.mtx).
 * g_timer_gen      – generation counter; increment to "cancel" any live timer.
 *                    The timer thread compares its captured generation against
 *                    this value before and after acquiring g_relay.mtx; if they
 *                    differ it exits without touching the relay.
 */
static std::atomic<bool> g_relay_is_open{false};
static std::atomic<int>  g_timer_gen{0};

/* ------------------------------------------------------------------ */
/* Command handlers                                                    */
/* ------------------------------------------------------------------ */

static std::string handle_relay_open() {
    std::lock_guard<std::mutex> lk(g_relay.mtx);
    if (g_relay.fd < 0) return json_unavailable();
    /* Cancel any running timed-open. */
    ++g_timer_gen;
    if (relay_open(g_relay.fd) == OK) { g_relay_is_open = true; return json_ok(); }
    g_relay.try_reconnect();
    if (g_relay.fd < 0) return json_unavailable();
    if (relay_open(g_relay.fd) == OK) { g_relay_is_open = true; return json_ok(); }
    return json_err("relay open failed");
}

static std::string handle_relay_close() {
    std::lock_guard<std::mutex> lk(g_relay.mtx);
    if (g_relay.fd < 0) return json_unavailable();
    /* Cancel any running timed-open. */
    ++g_timer_gen;
    if (relay_close(g_relay.fd) == OK) { g_relay_is_open = false; return json_ok(); }
    g_relay.try_reconnect();
    if (g_relay.fd < 0) return json_unavailable();
    if (relay_close(g_relay.fd) == OK) { g_relay_is_open = false; return json_ok(); }
    return json_err("relay close failed");
}

static std::string handle_relay_query() {
    std::lock_guard<std::mutex> lk(g_relay.mtx);
    if (g_relay.fd < 0) return json_unavailable();
    relay_state_t s = relay_query(g_relay.fd);
    if (s != RELAY_ERR) return json_relay_state(s);
    g_relay.try_reconnect();
    if (g_relay.fd < 0) return json_unavailable();
    s = relay_query(g_relay.fd);
    if (s != RELAY_ERR) return json_relay_state(s);
    return json_err("relay query failed");
}

static std::string handle_relay_open_timed(int duration_sec) {
    if (duration_sec <= 0 || duration_sec > 600)
        return json_err("duration out of range (1-600 seconds)");

    std::lock_guard<std::mutex> lk(g_relay.mtx);
    if (g_relay.fd < 0) return json_unavailable();

    /* If relay is already open: force-close and report error. */
    if (g_relay_is_open.load()) {
        ++g_timer_gen;                      /* cancel any live timer     */
        relay_close(g_relay.fd);            /* best-effort hardware close */
        g_relay_is_open = false;
        printf("[timed] relay was open, force-closed\n");
        return json_err("relay already open, has been closed");
    }

    /* Open relay. */
    if (relay_open(g_relay.fd) != OK) {
        g_relay.try_reconnect();
        if (g_relay.fd < 0) return json_unavailable();
        if (relay_open(g_relay.fd) != OK) return json_err("relay open failed");
    }
    g_relay_is_open = true;

    /* Arm new timer. */
    int gen = ++g_timer_gen;
    int dur = duration_sec;

    std::thread([gen, dur]() {
        /* Sleep for the requested duration. */
        for (int i = 0; i < dur && g_timer_gen.load() == gen; ++i)
            sleep(1);

        if (g_timer_gen.load() != gen)
            return;  /* cancelled by a later command */

        std::lock_guard<std::mutex> lk(g_relay.mtx);

        if (g_timer_gen.load() != gen)
            return;  /* cancelled while waiting for the lock */

        if (g_relay.fd >= 0) {
            relay_close(g_relay.fd);
            printf("[timed] relay auto-closed after %d seconds\n", dur);
        }
        g_relay_is_open = false;
    }).detach();

    return json_ok_timed(duration_sec);
}

static std::string handle_soil_query() {
    std::lock_guard<std::mutex> lk(g_soil.mtx);
    if (g_soil.fd < 0) return json_unavailable();
    soil_data_t d;
    if (soil_read(g_soil.fd, &d) == OK) return json_soil(d);
    int relay_fd;
    { std::lock_guard<std::mutex> lk2(g_relay.mtx); relay_fd = g_relay.fd; }
    g_soil.try_reconnect(relay_fd);
    if (g_soil.fd < 0) return json_unavailable();
    if (soil_read(g_soil.fd, &d) == OK) return json_soil(d);
    return json_err("soil read failed");
}

static std::string handle(const std::string &cmd, const std::string &raw_msg) {
    if (cmd == "relay_open")        return handle_relay_open();
    if (cmd == "relay_close")       return handle_relay_close();
    if (cmd == "relay_query")       return handle_relay_query();
    if (cmd == "relay_open_timed")  return handle_relay_open_timed(
                                        json_get_int(raw_msg, "duration"));
    if (cmd == "soil_query")        return handle_soil_query();
    return json_err("unknown command");
}

/* ------------------------------------------------------------------ */
/* Hot-plug scanner thread                                             */
/* ------------------------------------------------------------------ */

static void hotplug_thread() {
    while (g_running) {
        sleep(3);
        if (!g_running) break;

        {
            std::lock_guard<std::mutex> lk(g_relay.mtx);
            if (g_relay.fd < 0) {
                int fd = relay_device_init();
                if (fd >= 0) {
                    g_relay.fd = fd;
                    printf("[hotplug] relay reconnected\n");
                }
            }
        }

        {
            int relay_fd;
            { std::lock_guard<std::mutex> lk(g_relay.mtx); relay_fd = g_relay.fd; }

            std::lock_guard<std::mutex> lk(g_soil.mtx);
            if (g_soil.fd < 0) {
                int fd = soil_device_init(relay_fd);
                if (fd >= 0) {
                    g_soil.fd = fd;
                    printf("[hotplug] soil sensor reconnected\n");
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Signal handler                                                      */
/* ------------------------------------------------------------------ */

static void sig_handler(int) { g_running = false; }

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main() {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("detecting devices (each independently)...\n");
    g_relay.fd = relay_device_init();
    g_soil.fd  = soil_device_init(g_relay.fd);

    printf("relay: %s  soil: %s\n",
           g_relay.fd >= 0 ? "online" : "offline",
           g_soil.fd  >= 0 ? "online" : "offline");

    std::thread hp(hotplug_thread);

    printf("starting WebSocket server on port %u...\n", WS_PORT);

    rtc::WebSocketServerConfiguration cfg;
    cfg.port = WS_PORT;
    rtc::WebSocketServer server(cfg);

    server.onClient([](std::shared_ptr<rtc::WebSocket> ws) {
        auto addr = ws->remoteAddress().value_or("unknown");
        printf("client connected: %s\n", addr.c_str());

        ws->onMessage([ws, addr](rtc::message_variant msg) {
            if (!std::holds_alternative<std::string>(msg)) return;
            const std::string &text = std::get<std::string>(msg);
            printf("[%s] recv [%s] %s\n", now_str().c_str(), addr.c_str(), text.c_str());
            std::string cmd = json_get_string(text, "cmd");
            std::string reply = cmd.empty() ? json_err("missing 'cmd' field")
                                            : handle(cmd, text);
            printf("[%s] send [%s] %s\n", now_str().c_str(), addr.c_str(), reply.c_str());
            ws->send(reply);
        });

        ws->onClosed([addr]() { printf("client disconnected: %s\n", addr.c_str()); });
        ws->onError([addr](std::string e) {
            fprintf(stderr, "client error [%s]: %s\n", addr.c_str(), e.c_str());
        });
    });

    printf("listening. press Ctrl+C to stop.\n");
    while (g_running) sleep(1);

    ++g_timer_gen;  /* cancel any live timed-open on shutdown */

    g_running = false;
    hp.join();

    server.stop();
    relay_device_deinit(&g_relay.fd);
    soil_device_deinit(&g_soil.fd);
    printf("bye.\n");
    return 0;
}

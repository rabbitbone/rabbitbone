#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/memory.h>

namespace aurora {

class Console final {
public:
    static void write(const char *text) { console_write(text); }
    static void line(const char *text) { console_write(text); console_putc('\n'); }
};

class Memory final {
public:
    static memory_stats_t stats() {
        memory_stats_t s{};
        memory_get_stats(&s);
        return s;
    }
};

class Logger final {
public:
    static void info(const char *component, const char *text) { log_write(LOG_INFO, component, "%s", text); }
};

}

extern "C" void aurora_cpp_api_selftest(void) {
    aurora::Logger::info("cpp", "kernel C++ API facade ready");
    const memory_stats_t s = aurora::Memory::stats();
    if (s.frame_count == 0) aurora::Console::line("cpp api: memory stats unavailable");
}

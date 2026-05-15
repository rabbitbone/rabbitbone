#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/memory.h>

namespace rabbitbone {

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

extern "C" void rabbitbone_cpp_api_selftest(void) {
    rabbitbone::Logger::info("cpp", "kernel C++ API facade ready");
    const memory_stats_t s = rabbitbone::Memory::stats();
    if (s.frame_count == 0) rabbitbone::Console::line("cpp api: memory stats unavailable");
}

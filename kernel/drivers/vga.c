#include <aurora/drivers.h>
#include <aurora/kmem.h>
#include <aurora/libc.h>
#include <aurora/log.h>
#include <aurora/arch/io.h>

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_MEMORY ((volatile u16 *)0xb8000ull)
#define VGA_SCROLLBACK_DEFAULT_LINES 1024u
#define VGA_SCROLLBACK_MIN_LINES 64u
#define VGA_SCROLLBACK_EMERGENCY_LINES 16u

static usize row;
static usize col;
static u8 color;
static u16 (*screen_cells)[VGA_WIDTH];
static u16 (*scrollback_cells)[VGA_WIDTH];
static usize scrollback_capacity;
static usize scrollback_start;
static usize scrollback_count;
static u32 viewport_offset;
static bool cursor_visible_requested;
static bool hw_cursor_is_visible;
static u16 hw_cursor_last_pos = 0xffffu;

static u16 make_cell(char c) { return (u16)(u8)c | ((u16)color << 8); }
static u16 blank_cell(void) { return make_cell(' '); }
static u8 current_attr(void) { return color; }
static u16 recolor_cell(u16 cell, u8 attr) { return (u16)((cell & 0x00ffu) | ((u16)attr << 8)); }

static u16 hw_get_cell(usize y, usize x) { return VGA_MEMORY[y * VGA_WIDTH + x]; }
static void hw_set_cell(usize y, usize x, u16 cell) { VGA_MEMORY[y * VGA_WIDTH + x] = cell; }

static u16 live_get_cell(usize y, usize x) {
    if (screen_cells) return screen_cells[y][x];
    return hw_get_cell(y, x);
}

static void live_set_cell(usize y, usize x, u16 cell) {
    if (screen_cells) screen_cells[y][x] = cell;
    if (viewport_offset == 0) hw_set_cell(y, x, cell);
}

static void hw_cursor_hide(void) {
    if (!hw_cursor_is_visible) return;
    outb(0x3d4, 0x0a);
    outb(0x3d5, 0x20);
    hw_cursor_is_visible = false;
}

static void hw_cursor_show(void) {
    if (hw_cursor_is_visible) return;
    outb(0x3d4, 0x0a);
    outb(0x3d5, 0x0e);
    outb(0x3d4, 0x0b);
    outb(0x3d5, 0x0f);
    hw_cursor_is_visible = true;
}

static void hw_cursor_update(void) {
    if (!cursor_visible_requested || viewport_offset != 0) {
        hw_cursor_hide();
        return;
    }
    hw_cursor_show();
    u16 pos = (u16)(row * VGA_WIDTH + col);
    if (pos == hw_cursor_last_pos) return;
    outb(0x3d4, 0x0f);
    outb(0x3d5, (u8)(pos & 0xffu));
    outb(0x3d4, 0x0e);
    outb(0x3d5, (u8)((pos >> 8u) & 0xffu));
    hw_cursor_last_pos = pos;
}

static void clamp_cursor(void) {
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1u;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1u;
}

static void clear_live_line(usize y, usize from_col) {
    if (y >= VGA_HEIGHT || from_col >= VGA_WIDTH) return;
    u16 cell = blank_cell();
    for (usize x = from_col; x < VGA_WIDTH; ++x) live_set_cell(y, x, cell);
}

static void clear_scrollback(void) {
    scrollback_start = 0;
    scrollback_count = 0;
    viewport_offset = 0;
    if (!scrollback_cells || scrollback_capacity == 0) return;
    u16 cell = blank_cell();
    for (usize y = 0; y < scrollback_capacity; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) scrollback_cells[y][x] = cell;
    }
}

static void clear_live_screen(void) {
    for (usize y = 0; y < VGA_HEIGHT; ++y) clear_live_line(y, 0);
}

static void history_push_line(const u16 line[VGA_WIDTH]) {
    if (!scrollback_cells || scrollback_capacity == 0) return;
    usize pos;
    if (scrollback_count < scrollback_capacity) {
        pos = (scrollback_start + scrollback_count) % scrollback_capacity;
        ++scrollback_count;
    } else {
        pos = scrollback_start;
        scrollback_start = (scrollback_start + 1u) % scrollback_capacity;
    }
    for (usize x = 0; x < VGA_WIDTH; ++x) scrollback_cells[pos][x] = line[x];
}

static const u16 *history_line_at(usize idx) {
    if (!scrollback_cells || scrollback_capacity == 0 || idx >= scrollback_count) return 0;
    return scrollback_cells[(scrollback_start + idx) % scrollback_capacity];
}

static usize scrollback_max_offset(void) { return scrollback_cells ? scrollback_count : 0u; }

static void render_cells(usize y, const u16 cells[VGA_WIDTH]) {
    for (usize x = 0; x < VGA_WIDTH; ++x) hw_set_cell(y, x, cells[x]);
}

static void render_live_line(usize dst_y, usize live_y) {
    for (usize x = 0; x < VGA_WIDTH; ++x) hw_set_cell(dst_y, x, live_get_cell(live_y, x));
}

static void render_blank_line(usize y) {
    u16 cell = blank_cell();
    for (usize x = 0; x < VGA_WIDTH; ++x) hw_set_cell(y, x, cell);
}

static void render_view(void) {
    usize total = scrollback_count + VGA_HEIGHT;
    usize max_start = total > VGA_HEIGHT ? total - VGA_HEIGHT : 0u;
    usize start = scrollback_count;
    if (viewport_offset != 0) {
        usize off = viewport_offset > scrollback_max_offset() ? scrollback_max_offset() : viewport_offset;
        start = max_start > off ? max_start - off : 0u;
    }

    for (usize y = 0; y < VGA_HEIGHT; ++y) {
        usize idx = start + y;
        if (idx < scrollback_count) {
            const u16 *line = history_line_at(idx);
            if (line) render_cells(y, line); else render_blank_line(y);
        } else {
            usize live_y = idx - scrollback_count;
            if (live_y < VGA_HEIGHT) render_live_line(y, live_y); else render_blank_line(y);
        }
    }
    hw_cursor_update();
}

static void render_live_cell(usize y, usize x) {
    if (viewport_offset != 0 || y >= VGA_HEIGHT || x >= VGA_WIDTH) return;
    hw_set_cell(y, x, live_get_cell(y, x));
}

static void ensure_live_view(void) {
    if (viewport_offset != 0) {
        viewport_offset = 0;
        render_view();
    }
}

static void scroll_live_one(void) {
    u16 top[VGA_WIDTH];
    for (usize x = 0; x < VGA_WIDTH; ++x) top[x] = live_get_cell(0, x);
    history_push_line(top);
    for (usize y = 1; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) live_set_cell(y - 1u, x, live_get_cell(y, x));
    }
    clear_live_line(VGA_HEIGHT - 1u, 0);
    row = VGA_HEIGHT - 1u;
    render_view();
}

static void scroll_if_needed(void) {
    while (row >= VGA_HEIGHT) scroll_live_one();
}

static void copy_hw_to_screen(u16 target[VGA_HEIGHT][VGA_WIDTH]) {
    for (usize y = 0; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) target[y][x] = hw_get_cell(y, x);
    }
}

static bool allocate_screen_shadow(void) {
    if (screen_cells) return true;
    u16 (*screen)[VGA_WIDTH] = (u16 (*)[VGA_WIDTH])kmalloc(sizeof(u16) * VGA_HEIGHT * VGA_WIDTH);
    if (!screen) return false;
    copy_hw_to_screen(screen);
    screen_cells = screen;
    return true;
}

static bool allocate_history(usize lines) {
    if (lines == 0) return false;
    u16 (*history)[VGA_WIDTH] = (u16 (*)[VGA_WIDTH])kmalloc(sizeof(u16) * lines * VGA_WIDTH);
    if (!history) return false;
    scrollback_cells = history;
    scrollback_capacity = lines;
    clear_scrollback();
    return true;
}

void vga_set_color(u8 fg, u8 bg) { color = (u8)((bg << 4) | (fg & 0x0f)); }
u8 vga_get_color(void) { return color; }

void vga_recolor(u8 fg, u8 bg) {
    color = (u8)((bg << 4) | (fg & 0x0f));
    u8 attr = current_attr();
    for (usize y = 0; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) live_set_cell(y, x, recolor_cell(live_get_cell(y, x), attr));
    }
    if (scrollback_cells) {
        for (usize y = 0; y < scrollback_capacity; ++y) {
            for (usize x = 0; x < VGA_WIDTH; ++x) scrollback_cells[y][x] = recolor_cell(scrollback_cells[y][x], attr);
        }
    }
    render_view();
}

void vga_fill_color(u8 fg, u8 bg) {
    u8 old = color;
    vga_set_color(fg, bg);
    clear_live_screen();
    render_view();
    color = old;
    hw_cursor_update();
}

void vga_get_size(u32 *rows, u32 *cols) {
    if (rows) *rows = VGA_HEIGHT;
    if (cols) *cols = VGA_WIDTH;
}

void vga_move_cursor(u32 r, u32 c) {
    ensure_live_view();
    row = r < VGA_HEIGHT ? r : VGA_HEIGHT - 1u;
    col = c < VGA_WIDTH ? c : VGA_WIDTH - 1u;
    hw_cursor_update();
}

void vga_get_cursor(u32 *r, u32 *c) {
    if (r) *r = (u32)row;
    if (c) *c = (u32)col;
}

void vga_set_cursor_visible(bool visible) {
    cursor_visible_requested = visible;
    if (visible) hw_cursor_update();
    else hw_cursor_hide();
}

void vga_clear_line(void) {
    ensure_live_view();
    clamp_cursor();
    clear_live_line(row, col);
    for (usize x = col; x < VGA_WIDTH; ++x) render_live_cell(row, x);
    hw_cursor_update();
}

void vga_clear(void) {
    clear_scrollback();
    clear_live_screen();
    row = 0;
    col = 0;
    render_view();
}

void vga_init(void) {
    screen_cells = 0;
    scrollback_cells = 0;
    scrollback_capacity = 0;
    cursor_visible_requested = true;
    hw_cursor_is_visible = false;
    hw_cursor_last_pos = 0xffffu;
    vga_set_color(15, 1);
    vga_clear();
}

bool vga_enable_scrollback(void) {
    if (!allocate_screen_shadow()) return false;
    if (scrollback_cells && scrollback_capacity > 0) return true;

    for (usize lines = VGA_SCROLLBACK_DEFAULT_LINES; lines >= VGA_SCROLLBACK_MIN_LINES; lines /= 2u) {
        if (allocate_history(lines)) {
            KLOG(LOG_INFO, "tty", "scrollback lines=%llu", (unsigned long long)lines);
            return true;
        }
        if (lines == VGA_SCROLLBACK_MIN_LINES) break;
    }

    if (allocate_history(VGA_SCROLLBACK_EMERGENCY_LINES)) {
        KLOG(LOG_WARN, "tty", "large scrollback unavailable; using %u-line emergency buffer", VGA_SCROLLBACK_EMERGENCY_LINES);
        return true;
    }

    KLOG(LOG_WARN, "tty", "scrollback unavailable: unable to allocate even the emergency buffer");
    return false;
}

void vga_putc(char c) {
    ensure_live_view();
    clamp_cursor();
    if (c == '\r') {
        col = 0;
        hw_cursor_update();
        return;
    }
    if (c == '\n') {
        col = 0;
        ++row;
        scroll_if_needed();
        hw_cursor_update();
        return;
    }
    if (c == '\b') {
        if (col > 0) --col;
        clamp_cursor();
        live_set_cell(row, col, blank_cell());
        render_live_cell(row, col);
        hw_cursor_update();
        return;
    }
    live_set_cell(row, col, make_cell(c));
    render_live_cell(row, col);
    ++col;
    if (col >= VGA_WIDTH) {
        col = 0;
        ++row;
        scroll_if_needed();
    }
    hw_cursor_update();
}

bool vga_scroll_view(i32 delta) {
    if (delta == 0) return true;
    usize max = scrollback_max_offset();
    if (delta > 0) {
        u64 next = (u64)viewport_offset + (u64)(u32)delta;
        viewport_offset = next > (u64)max ? (u32)max : (u32)next;
    } else {
        u32 d = (u32)(-delta);
        viewport_offset = viewport_offset > d ? viewport_offset - d : 0u;
    }
    render_view();
    return true;
}

void vga_write_n(const char *s, usize n) { for (usize i = 0; i < n; ++i) vga_putc(s[i]); }
void vga_write(const char *s) { if (s) vga_write_n(s, strlen(s)); }

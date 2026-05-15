#include <rabbitbone/drivers.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/arch/io.h>

#define VGA_LEGACY_COLS 80u
#define VGA_LEGACY_ROWS 25u
#define VGA_FONT_W 8u
#define VGA_FONT_H 16u
#define VGA_MIN_COLS 40u
#define VGA_MIN_ROWS 12u
#define VGA_MAX_COLS 240u
#define VGA_MAX_ROWS 90u
#define VGA_MEMORY ((volatile u16 *)0xb8000ull)
#define VGA_SCROLLBACK_DEFAULT_LINES 1024u
#define VGA_SCROLLBACK_MIN_LINES 64u
#define VGA_SCROLLBACK_EMERGENCY_LINES 16u
#define VGA_FB_SCROLL_MIN_COALESCE 4u
#define VGA_FB_SCROLL_MAX_COALESCE 16u

static usize row;
static usize col;
static usize term_rows = VGA_LEGACY_ROWS;
static usize term_cols = VGA_LEGACY_COLS;
static u8 color;
static u16 *screen_cells;
static u16 *scrollback_cells;
static usize scrollback_capacity;
static usize scrollback_stride;
static usize scrollback_start;
static usize scrollback_count;
static u32 viewport_offset;
static bool cursor_visible_requested;
static bool hw_cursor_is_visible;
static u16 hw_cursor_last_pos = 0xffffu;
static u16 boot_screen_cells[VGA_MAX_ROWS * VGA_MAX_COLS];
static u16 resize_scratch_cells[VGA_MAX_ROWS * VGA_MAX_COLS];
static u32 update_depth;
static bool cursor_update_pending;
static bool fb_full_redraw_pending;
static usize fb_scroll_rows_pending;

typedef struct framebuffer_console {
    bool active;
    volatile u32 *pixels;
    u32 width;
    u32 height;
    u32 pitch_pixels;
    u32 format;
    bool cursor_drawn;
    usize cursor_row;
    usize cursor_col;
} framebuffer_console_t;

static framebuffer_console_t fb;

static usize text_rows(void) { return term_rows ? term_rows : VGA_LEGACY_ROWS; }
static usize text_cols(void) { return term_cols ? term_cols : VGA_LEGACY_COLS; }
#define VGA_HEIGHT text_rows()
#define VGA_WIDTH text_cols()

static usize cell_index(usize y, usize x) { return y * VGA_WIDTH + x; }
static usize cell_count(void) { return VGA_HEIGHT * VGA_WIDTH; }
static usize max_cell_count(void) { return (usize)VGA_MAX_ROWS * (usize)VGA_MAX_COLS; }
static usize clamp_dim(usize value, usize min, usize max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static const u8 fb_font5x7[128][7] = {
    [' '] = {0, 0, 0, 0, 0, 0, 0},
    ['!'] = {4, 4, 4, 4, 4, 0, 4},
    ['"'] = {10, 10, 10, 0, 0, 0, 0},
    ['#'] = {10, 10, 31, 10, 31, 10, 10},
    ['$'] = {4, 15, 20, 14, 5, 30, 4},
    ['%'] = {24, 25, 2, 4, 8, 19, 3},
    ['&'] = {12, 18, 20, 8, 21, 18, 13},
    ['\''] = {4, 4, 8, 0, 0, 0, 0},
    ['('] = {2, 4, 8, 8, 8, 4, 2},
    [')'] = {8, 4, 2, 2, 2, 4, 8},
    ['*'] = {0, 4, 21, 14, 21, 4, 0},
    ['+'] = {0, 4, 4, 31, 4, 4, 0},
    [','] = {0, 0, 0, 0, 4, 4, 8},
    ['-'] = {0, 0, 0, 31, 0, 0, 0},
    ['.'] = {0, 0, 0, 0, 0, 6, 6},
    ['/'] = {1, 2, 4, 8, 16, 0, 0},
    ['0'] = {14, 17, 19, 21, 25, 17, 14},
    ['1'] = {4, 12, 4, 4, 4, 4, 14},
    ['2'] = {14, 17, 1, 2, 4, 8, 31},
    ['3'] = {30, 1, 1, 14, 1, 1, 30},
    ['4'] = {2, 6, 10, 18, 31, 2, 2},
    ['5'] = {31, 16, 30, 1, 1, 17, 14},
    ['6'] = {6, 8, 16, 30, 17, 17, 14},
    ['7'] = {31, 1, 2, 4, 8, 8, 8},
    ['8'] = {14, 17, 17, 14, 17, 17, 14},
    ['9'] = {14, 17, 17, 15, 1, 2, 12},
    [':'] = {0, 6, 6, 0, 6, 6, 0},
    [';'] = {0, 6, 6, 0, 6, 6, 8},
    ['<'] = {2, 4, 8, 16, 8, 4, 2},
    ['='] = {0, 0, 31, 0, 31, 0, 0},
    ['>'] = {8, 4, 2, 1, 2, 4, 8},
    ['?'] = {14, 17, 1, 2, 4, 0, 4},
    ['@'] = {14, 17, 1, 13, 21, 21, 14},
    ['A'] = {14, 17, 17, 31, 17, 17, 17},
    ['B'] = {30, 17, 17, 30, 17, 17, 30},
    ['C'] = {14, 17, 16, 16, 16, 17, 14},
    ['D'] = {30, 17, 17, 17, 17, 17, 30},
    ['E'] = {31, 16, 16, 30, 16, 16, 31},
    ['F'] = {31, 16, 16, 30, 16, 16, 16},
    ['G'] = {14, 17, 16, 23, 17, 17, 15},
    ['H'] = {17, 17, 17, 31, 17, 17, 17},
    ['I'] = {14, 4, 4, 4, 4, 4, 14},
    ['J'] = {7, 2, 2, 2, 2, 18, 12},
    ['K'] = {17, 18, 20, 24, 20, 18, 17},
    ['L'] = {16, 16, 16, 16, 16, 16, 31},
    ['M'] = {17, 27, 21, 21, 17, 17, 17},
    ['N'] = {17, 25, 21, 19, 17, 17, 17},
    ['O'] = {14, 17, 17, 17, 17, 17, 14},
    ['P'] = {30, 17, 17, 30, 16, 16, 16},
    ['Q'] = {14, 17, 17, 17, 21, 18, 13},
    ['R'] = {30, 17, 17, 30, 20, 18, 17},
    ['S'] = {15, 16, 16, 14, 1, 1, 30},
    ['T'] = {31, 4, 4, 4, 4, 4, 4},
    ['U'] = {17, 17, 17, 17, 17, 17, 14},
    ['V'] = {17, 17, 17, 17, 17, 10, 4},
    ['W'] = {17, 17, 17, 21, 21, 21, 10},
    ['X'] = {17, 17, 10, 4, 10, 17, 17},
    ['Y'] = {17, 17, 10, 4, 4, 4, 4},
    ['Z'] = {31, 1, 2, 4, 8, 16, 31},
    ['['] = {14, 8, 8, 8, 8, 8, 14},
    ['\\'] = {16, 8, 4, 2, 1, 0, 0},
    [']'] = {14, 2, 2, 2, 2, 2, 14},
    ['^'] = {4, 10, 17, 0, 0, 0, 0},
    ['_'] = {0, 0, 0, 0, 0, 0, 31},
    ['`'] = {8, 4, 2, 0, 0, 0, 0},
    ['a'] = {0, 0, 14, 1, 15, 17, 15},
    ['b'] = {16, 16, 30, 17, 17, 17, 30},
    ['c'] = {0, 0, 14, 16, 16, 17, 14},
    ['d'] = {1, 1, 15, 17, 17, 17, 15},
    ['e'] = {0, 0, 14, 17, 31, 16, 14},
    ['f'] = {6, 8, 8, 30, 8, 8, 8},
    ['g'] = {0, 0, 15, 17, 17, 15, 1},
    ['h'] = {16, 16, 30, 17, 17, 17, 17},
    ['i'] = {4, 0, 12, 4, 4, 4, 14},
    ['j'] = {2, 0, 6, 2, 2, 18, 12},
    ['k'] = {16, 16, 18, 20, 24, 20, 18},
    ['l'] = {12, 4, 4, 4, 4, 4, 14},
    ['m'] = {0, 0, 26, 21, 21, 17, 17},
    ['n'] = {0, 0, 30, 17, 17, 17, 17},
    ['o'] = {0, 0, 14, 17, 17, 17, 14},
    ['p'] = {0, 0, 30, 17, 17, 30, 16},
    ['q'] = {0, 0, 15, 17, 17, 15, 1},
    ['r'] = {0, 0, 22, 24, 16, 16, 16},
    ['s'] = {0, 0, 15, 16, 14, 1, 30},
    ['t'] = {8, 8, 30, 8, 8, 9, 6},
    ['u'] = {0, 0, 17, 17, 17, 17, 15},
    ['v'] = {0, 0, 17, 17, 17, 10, 4},
    ['w'] = {0, 0, 17, 17, 21, 21, 10},
    ['x'] = {0, 0, 17, 10, 4, 10, 17},
    ['y'] = {0, 0, 17, 17, 17, 15, 1},
    ['z'] = {0, 0, 31, 2, 4, 8, 31},
    ['{'] = {2, 4, 4, 8, 4, 4, 2},
    ['|'] = {4, 4, 4, 4, 4, 4, 4},
    ['}'] = {8, 4, 4, 2, 4, 4, 8},
    ['~'] = {0, 0, 8, 21, 2, 0, 0},
};


static const u32 fb_vga_rgb[16] = {
    0x000000u, 0x0000aau, 0x00aa00u, 0x00aaaau,
    0xaa0000u, 0xaa00aau, 0xaa5500u, 0xaaaaaau,
    0x555555u, 0x5555ffu, 0x55ff55u, 0x55ffffu,
    0xff5555u, 0xff55ffu, 0xffff55u, 0xffffffu,
};

static u32 fb_pack_rgb(u32 rgb) {
    u32 r = (rgb >> 16u) & 0xffu;
    u32 g = (rgb >> 8u) & 0xffu;
    u32 b = rgb & 0xffu;
    if (fb.format == RABBITBONE_BOOT_FB_FORMAT_RGBX) return r | (g << 8u) | (b << 16u);
    return b | (g << 8u) | (r << 16u);
}

static u32 fb_attr_fg(u8 attr) { return fb_pack_rgb(fb_vga_rgb[attr & 0x0fu]); }
static u32 fb_attr_bg(u8 attr) { return fb_pack_rgb(fb_vga_rgb[(attr >> 4u) & 0x0fu]); }

static void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 pixel) {
    if (!fb.active || !fb.pixels || x >= fb.width || y >= fb.height) return;
    if (w > fb.width - x) w = fb.width - x;
    if (h > fb.height - y) h = fb.height - y;
    for (u32 yy = 0; yy < h; ++yy) {
        volatile u32 *rowp = fb.pixels + (usize)(y + yy) * fb.pitch_pixels + x;
        for (u32 xx = 0; xx < w; ++xx) rowp[xx] = pixel;
    }
}

static u16 make_cell(char c) { return (u16)(u8)c | ((u16)color << 8); }
static u16 blank_cell(void) { return make_cell(' '); }
static u8 current_attr(void) { return color; }
static u16 recolor_cell(u16 cell, u8 attr) { return (u16)((cell & 0x00ffu) | ((u16)attr << 8)); }

static u16 sanitize_cell(u16 cell) {
    u8 ch = (u8)(cell & 0x00ffu);
    u8 attr = (u8)(cell >> 8u);
    if (ch == 0u) ch = (u8)' ';
    /* UEFI framebuffer mode has no real text VRAM.  Early scrollback may
     * contain zero-filled shadow cells if 0xb8000 was read before the shadow
     * buffer was fully owned by the console.  Treat invisible black-on-black
     * cells as normal-theme cells so PageUp never paints a stale black block. */
    if (attr == 0u) attr = current_attr();
    return (u16)ch | ((u16)attr << 8u);
}

static void fb_draw_cell(usize y, usize x, u16 cell);
static void fb_draw_cursor(void);
static void fb_redraw_cursor_cell(void);
static void fb_flush_pending_scrolls(bool force);
static u16 hw_get_cell(usize y, usize x) {
    if (y >= VGA_LEGACY_ROWS || x >= VGA_LEGACY_COLS) return blank_cell();
    return sanitize_cell(VGA_MEMORY[y * VGA_LEGACY_COLS + x]);
}
static void hw_set_cell(usize y, usize x, u16 cell) {
    cell = sanitize_cell(cell);
    if (fb.active) {
        fb_draw_cell(y, x, cell);
        return;
    }
    if (y < VGA_LEGACY_ROWS && x < VGA_LEGACY_COLS) {
        VGA_MEMORY[y * VGA_LEGACY_COLS + x] = cell;
    }
}

static u16 live_get_cell(usize y, usize x) {
    if (screen_cells) return sanitize_cell(screen_cells[cell_index(y, x)]);
    return hw_get_cell(y, x);
}


static void fb_draw_cell(usize y, usize x, u16 cell) {
    if (!fb.active || !fb.pixels || x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    cell = sanitize_cell(cell);
    u32 px = (u32)x * VGA_FONT_W;
    u32 py = (u32)y * VGA_FONT_H;
    if (px + VGA_FONT_W > fb.width || py + VGA_FONT_H > fb.height) return;
    u8 attr = (u8)(cell >> 8u);
    u32 fg = fb_attr_fg(attr);
    u32 bg = fb_attr_bg(attr);
    for (u32 yy = 0; yy < VGA_FONT_H; ++yy) {
        volatile u32 *rowp = fb.pixels + (usize)(py + yy) * fb.pitch_pixels + px;
        for (u32 xx = 0; xx < VGA_FONT_W; ++xx) rowp[xx] = bg;
    }
    unsigned char ch = (unsigned char)(cell & 0xffu);
    if (ch == (unsigned char)' ') return;
    if (ch >= 128u || ch < 32u) ch = (unsigned char)'?';
    for (u32 gy = 0; gy < 7u; ++gy) {
        u8 bits = fb_font5x7[ch][gy];
        volatile u32 *r0 = fb.pixels + (usize)(py + 1u + gy * 2u) * fb.pitch_pixels + px + 1u;
        volatile u32 *r1 = r0 + fb.pitch_pixels;
        for (u32 gx = 0; gx < 5u; ++gx) {
            if (bits & (1u << (4u - gx))) {
                r0[gx] = fg;
                r1[gx] = fg;
            }
        }
    }
}

static void fb_clear_full(u8 attr) {
    if (!fb.active || !fb.pixels) return;
    fb_fill_rect(0, 0, fb.width, fb.height, fb_attr_bg(attr));
}

static void fb_redraw_cursor_cell(void) {
    if (!fb.active || !fb.cursor_drawn) return;
    fb.cursor_drawn = false;
    if (fb.cursor_row < VGA_HEIGHT && fb.cursor_col < VGA_WIDTH) {
        fb_draw_cell(fb.cursor_row, fb.cursor_col, live_get_cell(fb.cursor_row, fb.cursor_col));
    }
}

static void fb_draw_cursor(void) {
    if (!fb.active || !cursor_visible_requested || viewport_offset != 0) return;
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    u16 cell = live_get_cell(row, col);
    u8 attr = (u8)(cell >> 8u);
    u32 fg = fb_attr_fg(attr);
    u32 px = (u32)col * VGA_FONT_W;
    u32 py = (u32)row * VGA_FONT_H;
    fb_fill_rect(px + 1u, py + VGA_FONT_H - 2u, VGA_FONT_W - 2u, 1u, fg);
    fb.cursor_row = row;
    fb.cursor_col = col;
    fb.cursor_drawn = true;
}
static void live_store_cell(usize y, usize x, u16 cell) {
    cell = sanitize_cell(cell);
    if (screen_cells) screen_cells[cell_index(y, x)] = cell;
    else if (!fb.active && y < VGA_LEGACY_ROWS && x < VGA_LEGACY_COLS) VGA_MEMORY[y * VGA_LEGACY_COLS + x] = cell;
}

static void live_set_cell(usize y, usize x, u16 cell) {
    cell = sanitize_cell(cell);
    if (screen_cells) screen_cells[cell_index(y, x)] = cell;
    if (viewport_offset == 0 && !(fb.active && (fb_full_redraw_pending || fb_scroll_rows_pending != 0))) hw_set_cell(y, x, cell);
}

static void hw_cursor_hide(void) {
    if (fb.active) { fb_redraw_cursor_cell(); return; }
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
    if (update_depth != 0) {
        cursor_update_pending = true;
        return;
    }
    cursor_update_pending = false;
    if (fb.active && fb_scroll_rows_pending != 0) {
        cursor_update_pending = true;
        return;
    }
    if (fb.active) {
        fb_redraw_cursor_cell();
        if (cursor_visible_requested && viewport_offset == 0) fb_draw_cursor();
        return;
    }
    if (!cursor_visible_requested || viewport_offset != 0) {
        hw_cursor_hide();
        return;
    }
    hw_cursor_show();
    u16 pos = (u16)(row * VGA_LEGACY_COLS + col);
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

static bool scrollback_usable(void) {
    return scrollback_cells && scrollback_capacity != 0 && scrollback_stride == VGA_WIDTH;
}

static void clear_scrollback(void) {
    scrollback_start = 0;
    scrollback_count = 0;
    viewport_offset = 0;
    if (!scrollback_cells || scrollback_capacity == 0 || scrollback_stride == 0) return;
    u16 cell = blank_cell();
    for (usize y = 0; y < scrollback_capacity; ++y) {
        for (usize x = 0; x < scrollback_stride; ++x) scrollback_cells[y * scrollback_stride + x] = cell;
    }
}

static void clear_live_screen(void) {
    u16 cell = blank_cell();
    for (usize y = 0; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) live_store_cell(y, x, cell);
    }
}

static void history_push_line(const u16 *line) {
    if (!scrollback_usable()) return;
    usize pos;
    if (scrollback_count < scrollback_capacity) {
        pos = (scrollback_start + scrollback_count) % scrollback_capacity;
        ++scrollback_count;
    } else {
        pos = scrollback_start;
        scrollback_start = (scrollback_start + 1u) % scrollback_capacity;
    }
    for (usize x = 0; x < VGA_WIDTH; ++x) scrollback_cells[pos * scrollback_stride + x] = sanitize_cell(line[x]);
}

static const u16 *history_line_at(usize idx) {
    if (!scrollback_usable() || idx >= scrollback_count) return 0;
    return scrollback_cells + ((scrollback_start + idx) % scrollback_capacity) * scrollback_stride;
}

static usize scrollback_max_offset(void) { return scrollback_usable() ? scrollback_count : 0u; }

static void render_cells(usize y, const u16 *cells) {
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
    fb_scroll_rows_pending = 0;
    fb_full_redraw_pending = false;
    usize visible_scrollback_count = scrollback_usable() ? scrollback_count : 0u;
    usize total = visible_scrollback_count + VGA_HEIGHT;
    usize max_start = total > VGA_HEIGHT ? total - VGA_HEIGHT : 0u;
    usize start = visible_scrollback_count;
    if (viewport_offset != 0) {
        usize off = viewport_offset > scrollback_max_offset() ? scrollback_max_offset() : viewport_offset;
        start = max_start > off ? max_start - off : 0u;
    }

    for (usize y = 0; y < VGA_HEIGHT; ++y) {
        usize idx = start + y;
        if (idx < visible_scrollback_count) {
            const u16 *line = history_line_at(idx);
            if (line) render_cells(y, line); else render_blank_line(y);
        } else {
            usize live_y = idx - visible_scrollback_count;
            if (live_y < VGA_HEIGHT) render_live_line(y, live_y); else render_blank_line(y);
        }
    }
    hw_cursor_update();
}


static void ensure_live_view(void) {
    if (viewport_offset != 0) {
        viewport_offset = 0;
        render_view();
    }
}

static void fb_scroll_text_area_up_rows(usize rows_to_scroll) {
    if (!fb.active || !fb.pixels || rows_to_scroll == 0) return;
    if (fb.width < VGA_WIDTH * VGA_FONT_W || fb.height < VGA_HEIGHT * VGA_FONT_H) return;
    if (rows_to_scroll >= VGA_HEIGHT) {
        fb_clear_full(current_attr());
        return;
    }
    u32 text_w = (u32)VGA_WIDTH * VGA_FONT_W;
    u32 text_h = (u32)VGA_HEIGHT * VGA_FONT_H;
    u32 dy = (u32)rows_to_scroll * VGA_FONT_H;
    for (u32 yy = 0; yy < text_h - dy; ++yy) {
        volatile u32 *dst = fb.pixels + (usize)yy * fb.pitch_pixels;
        volatile u32 *src = fb.pixels + (usize)(yy + dy) * fb.pitch_pixels;
        for (u32 xx = 0; xx < text_w; ++xx) dst[xx] = src[xx];
    }
    fb_fill_rect(0, text_h - dy, text_w, dy, fb_attr_bg(current_attr()));
}

static usize fb_scroll_coalesce_threshold(void) {
    usize threshold = VGA_HEIGHT / 6u;
    if (threshold < VGA_FB_SCROLL_MIN_COALESCE) threshold = VGA_FB_SCROLL_MIN_COALESCE;
    if (threshold > VGA_FB_SCROLL_MAX_COALESCE) threshold = VGA_FB_SCROLL_MAX_COALESCE;
    return threshold;
}

static void fb_flush_pending_scrolls(bool force) {
    if (!fb.active || fb_scroll_rows_pending == 0) return;
    if (!force && fb_scroll_rows_pending < fb_scroll_coalesce_threshold()) return;
    if (viewport_offset != 0 || fb_scroll_rows_pending >= VGA_HEIGHT || fb_full_redraw_pending) {
        fb_scroll_rows_pending = 0;
        fb_full_redraw_pending = false;
        render_view();
        return;
    }
    usize rows_to_scroll = fb_scroll_rows_pending;
    fb_scroll_rows_pending = 0;
    fb_scroll_text_area_up_rows(rows_to_scroll);
    usize first = VGA_HEIGHT - rows_to_scroll;
    for (usize y = first; y < VGA_HEIGHT; ++y) render_live_line(y, y);
    hw_cursor_update();
}

static void scroll_live_one(void) {
    if (viewport_offset == 0 && fb.active && fb_scroll_rows_pending == 0) fb_redraw_cursor_cell();
    u16 top[VGA_WIDTH];
    for (usize x = 0; x < VGA_WIDTH; ++x) top[x] = live_get_cell(0, x);
    history_push_line(top);
    for (usize y = 1; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) live_store_cell(y - 1u, x, live_get_cell(y, x));
    }
    u16 blank = blank_cell();
    for (usize x = 0; x < VGA_WIDTH; ++x) live_store_cell(VGA_HEIGHT - 1u, x, blank);
    row = VGA_HEIGHT - 1u;
    if (viewport_offset == 0 && fb.active) {
        if (fb_scroll_rows_pending < VGA_HEIGHT) ++fb_scroll_rows_pending;
        else fb_full_redraw_pending = true;
        if (update_depth != 0) {
            cursor_update_pending = true;
        } else {
            fb_flush_pending_scrolls(true);
        }
    } else {
        render_view();
    }
}

static void scroll_if_needed(void) {
    while (row >= VGA_HEIGHT) scroll_live_one();
}

static void copy_hw_to_screen(u16 *target, usize rows, usize cols) {
    if (!target) return;
    for (usize y = 0; y < rows; ++y) {
        for (usize x = 0; x < cols; ++x) target[y * cols + x] = sanitize_cell(hw_get_cell(y, x));
    }
}

static bool allocate_screen_shadow(void) {
    if (screen_cells) return true;
    u16 *screen = (u16 *)kmalloc(sizeof(u16) * cell_count());
    if (!screen) return false;
    copy_hw_to_screen(screen, VGA_HEIGHT, VGA_WIDTH);
    screen_cells = screen;
    return true;
}

static bool allocate_history(usize lines) {
    if (lines == 0) return false;
    usize cells = 0;
    usize bytes = 0;
    if (__builtin_mul_overflow(lines, VGA_WIDTH, &cells)) return false;
    if (__builtin_mul_overflow(cells, sizeof(u16), &bytes)) return false;
    u16 *history = (u16 *)kmalloc(bytes);
    if (!history) return false;
    scrollback_cells = history;
    scrollback_capacity = lines;
    scrollback_stride = VGA_WIDTH;
    clear_scrollback();
    return true;
}

void vga_set_color(u8 fg, u8 bg) { color = (u8)(((bg & 0x0f) << 4) | (fg & 0x0f)); }
u8 vga_get_color(void) { return color; }

void vga_recolor(u8 fg, u8 bg) {
    vga_set_color(fg, bg);
    u8 attr = current_attr();
    for (usize y = 0; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) live_store_cell(y, x, recolor_cell(live_get_cell(y, x), attr));
    }
    if (scrollback_cells && scrollback_stride != 0) {
        for (usize y = 0; y < scrollback_capacity; ++y) {
            for (usize x = 0; x < scrollback_stride; ++x) {
                usize idx = y * scrollback_stride + x;
                scrollback_cells[idx] = recolor_cell(scrollback_cells[idx], attr);
            }
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
    if (fb.active && fb_scroll_rows_pending != 0) fb_flush_pending_scrolls(true);
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
    if (fb.active && fb_scroll_rows_pending != 0) fb_flush_pending_scrolls(true);
    ensure_live_view();
    clamp_cursor();
    clear_live_line(row, col);
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
    fb.active = false;
    fb.pixels = 0;
    fb.width = 0;
    fb.height = 0;
    fb.pitch_pixels = 0;
    fb.format = 0;
    fb.cursor_drawn = false;
    term_rows = VGA_LEGACY_ROWS;
    term_cols = VGA_LEGACY_COLS;
    screen_cells = boot_screen_cells;
    scrollback_cells = 0;
    scrollback_capacity = 0;
    scrollback_stride = 0;
    scrollback_start = 0;
    scrollback_count = 0;
    viewport_offset = 0;
    update_depth = 0;
    cursor_update_pending = false;
    fb_full_redraw_pending = false;
    fb_scroll_rows_pending = 0;
    cursor_visible_requested = true;
    hw_cursor_is_visible = false;
    hw_cursor_last_pos = 0xffffu;
    vga_set_color(15, 1);
    vga_clear();
}

bool vga_enable_scrollback(void) {
    if (!allocate_screen_shadow()) return false;
    if (scrollback_usable()) return true;
    if (scrollback_cells) {
        kfree(scrollback_cells);
        scrollback_cells = 0;
        scrollback_capacity = 0;
        scrollback_stride = 0;
        scrollback_start = 0;
        scrollback_count = 0;
        viewport_offset = 0;
    }

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
        hw_cursor_update();
        return;
    }
    live_set_cell(row, col, make_cell(c));
    ++col;
    if (col >= VGA_WIDTH) {
        col = 0;
        ++row;
        scroll_if_needed();
    }
    hw_cursor_update();
}

bool vga_scroll_view(i32 delta) {
    if (fb.active && fb_scroll_rows_pending != 0) fb_flush_pending_scrolls(true);
    if (delta == 0) return true;
    usize max = scrollback_max_offset();
    if (delta > 0) {
        u64 next = (u64)viewport_offset + (u64)(u32)delta;
        viewport_offset = next > (u64)max ? (u32)max : (u32)next;
    } else {
        u64 d = (u64)(-(i64)delta);
        viewport_offset = (u64)viewport_offset > d ? (u32)((u64)viewport_offset - d) : 0u;
    }
    render_view();
    return true;
}


static void reconfigure_text_grid(usize new_rows, usize new_cols) {
    new_rows = clamp_dim(new_rows, VGA_MIN_ROWS, VGA_MAX_ROWS);
    new_cols = clamp_dim(new_cols, VGA_MIN_COLS, VGA_MAX_COLS);
    if (new_rows == VGA_HEIGHT && new_cols == VGA_WIDTH) return;
    u16 blank = blank_cell();
    for (usize i = 0; i < max_cell_count(); ++i) resize_scratch_cells[i] = blank;
    usize old_rows = VGA_HEIGHT;
    usize old_cols = VGA_WIDTH;
    usize copy_rows = old_rows < new_rows ? old_rows : new_rows;
    usize copy_cols = old_cols < new_cols ? old_cols : new_cols;
    bool had_scrollback = scrollback_cells && scrollback_capacity != 0;
    usize old_scrollback_capacity = scrollback_capacity;
    if (had_scrollback && old_cols != new_cols) {
        kfree(scrollback_cells);
        scrollback_cells = 0;
        scrollback_capacity = 0;
        scrollback_stride = 0;
        scrollback_start = 0;
        scrollback_count = 0;
        viewport_offset = 0;
    }
    if (screen_cells) {
        for (usize y = 0; y < copy_rows; ++y) {
            for (usize x = 0; x < copy_cols; ++x) {
                resize_scratch_cells[y * new_cols + x] = sanitize_cell(screen_cells[y * old_cols + x]);
            }
        }
        term_rows = new_rows;
        term_cols = new_cols;
        for (usize i = 0; i < cell_count(); ++i) screen_cells[i] = sanitize_cell(resize_scratch_cells[i]);
    } else {
        term_rows = new_rows;
        term_cols = new_cols;
    }
    if (had_scrollback && old_cols != new_cols) (void)allocate_history(old_scrollback_capacity);
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1u;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1u;
    clear_scrollback();
}

static void reconfigure_text_grid_for_framebuffer(u32 width, u32 height) {
    usize cols = (usize)(width / VGA_FONT_W);
    usize rows = (usize)(height / VGA_FONT_H);
    reconfigure_text_grid(rows, cols);
}


static bool fb_map_identity(uptr base, usize bytes) {
    if (base == 0 || bytes == 0) return false;
    uptr end_raw = 0;
    if (__builtin_add_overflow(base, (uptr)bytes, &end_raw)) return false;
    uptr end = 0;
    if (!rabbitbone_align_up_usize_checked((usize)end_raw, PAGE_SIZE, (usize *)&end)) return false;
    uptr start = base & ~(uptr)(PAGE_SIZE - 1u);
    for (uptr page = start; page < end; page += PAGE_SIZE) {
        uptr phys = 0;
        u64 flags = 0;
        if (vmm_translate(page, &phys, &flags) && phys == page && (flags & VMM_PRESENT)) continue;
        if (!vmm_map_4k(page, page, VMM_WRITE | VMM_NOCACHE | VMM_NX)) return false;
    }
    return true;
}

static bool boot_framebuffer_fields(const rabbitbone_bootinfo_t *bootinfo, uptr *base_out, u32 *width_out, u32 *height_out, u32 *pitch_out, u32 *format_out) {
    if (!bootinfo_basic_usable(bootinfo)) return false;
    uptr base = (uptr)RABBITBONE_BOOT_FB_BASE(bootinfo);
    u32 width = RABBITBONE_BOOT_FB_WIDTH(bootinfo);
    u32 height = RABBITBONE_BOOT_FB_HEIGHT(bootinfo);
    u32 pitch = RABBITBONE_BOOT_FB_PITCH_PIXELS(bootinfo);
    u32 format = RABBITBONE_BOOT_FB_FORMAT(bootinfo);
    if (base == 0 || width < VGA_MIN_COLS * VGA_FONT_W || height < VGA_MIN_ROWS * VGA_FONT_H || pitch < width) return false;
    if (format != RABBITBONE_BOOT_FB_FORMAT_RGBX && format != RABBITBONE_BOOT_FB_FORMAT_BGRX) return false;
    if (base_out) *base_out = base;
    if (width_out) *width_out = width;
    if (height_out) *height_out = height;
    if (pitch_out) *pitch_out = pitch;
    if (format_out) *format_out = format;
    return true;
}

static bool vga_enable_boot_framebuffer_raw(uptr base, u32 width, u32 height, u32 pitch, u32 format) {
    if (fb.active) {
        return (uptr)fb.pixels == base && fb.width == width && fb.height == height &&
               fb.pitch_pixels == pitch && fb.format == format;
    }
    usize rows_bytes = 0;
    usize bytes = 0;
    if (__builtin_mul_overflow((usize)pitch, sizeof(u32), &rows_bytes)) return false;
    if (__builtin_mul_overflow(rows_bytes, (usize)height, &bytes)) return false;
    if (!fb_map_identity(base, bytes)) return false;
    reconfigure_text_grid_for_framebuffer(width, height);
    fb.active = true;
    fb.pixels = (volatile u32 *)base;
    fb.width = width;
    fb.height = height;
    fb.pitch_pixels = pitch;
    fb.format = format;
    fb.cursor_drawn = false;
    fb.cursor_row = 0;
    fb.cursor_col = 0;
    fb_full_redraw_pending = false;
    fb_scroll_rows_pending = 0;
    fb_clear_full(color);
    render_view();
    KLOG(LOG_INFO, "fbcon", "UEFI framebuffer enabled base=%p %ux%u pitch=%u format=%u", (void *)base, width, height, pitch, format);
    return true;
}

bool vga_use_boot_framebuffer_early(const rabbitbone_bootinfo_t *bootinfo) {
    uptr base = 0;
    u32 width = 0, height = 0, pitch = 0, format = 0;
    if (!boot_framebuffer_fields(bootinfo, &base, &width, &height, &pitch, &format)) return false;
    return vga_enable_boot_framebuffer_raw(base, width, height, pitch, format);
}

bool vga_use_boot_framebuffer(const rabbitbone_bootinfo_t *bootinfo) {
    if (!bootinfo_validate(bootinfo)) return false;
    return vga_use_boot_framebuffer_early(bootinfo);
}

void vga_begin_update(void) {
    if (update_depth == 0 && fb.active && fb.cursor_drawn) {
        /* The framebuffer cursor is painted into the same pixels as text.
         * Remove it once at the start of a batched update so fast output and
         * scroll-copy never drag the underline through the text area. */
        fb_redraw_cursor_cell();
    }
    if (update_depth != ~0u) ++update_depth;
    else cursor_update_pending = true;
}

void vga_end_update(void) {
    if (update_depth == 0) return;
    --update_depth;
    if (update_depth == 0) {
        if (fb.active && fb_scroll_rows_pending != 0) fb_flush_pending_scrolls(false);
        if (fb.active && fb_full_redraw_pending) {
            render_view();
        } else if (cursor_update_pending && fb_scroll_rows_pending == 0) {
            hw_cursor_update();
        }
    }
}

void vga_flush(void) {
    if (fb.active && fb_scroll_rows_pending != 0) fb_flush_pending_scrolls(true);
    if (fb.active && fb_full_redraw_pending) render_view();
    else if (cursor_update_pending) hw_cursor_update();
}

void vga_write_n(const char *s, usize n) {
    if (!s && n) return;
    vga_begin_update();
    for (usize i = 0; i < n; ++i) vga_putc(s[i]);
    vga_end_update();
}
void vga_write(const char *s) { if (s) vga_write_n(s, strlen(s)); }

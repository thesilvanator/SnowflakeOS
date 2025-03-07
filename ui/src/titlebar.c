#include <ui.h>
#include <snow.h>

#include <string.h>
#include <stdlib.h>

void titlebar_on_draw(titlebar_t* tb, fb_t fb) {
    rect_t r = ui_get_absolute_bounds(W(tb));
    uint32_t margin = (WM_TB_HEIGHT - 16) / 2;
    uint32_t tb_color = 0x303030;

    if (tb->hovered) {
        tb_color = ui_shade_color(tb_color, 20);
    }

    snow_draw_rect(fb, r.x, r.y, r.w, r.h, tb_color); // TODO: use color scheme

    if (tb->icon) {
        snow_draw_rgb_masked(fb, tb->icon,
            r.x + margin, r.y + margin, 16, 16, 0xFFFFFF);
        snow_draw_string(fb, tb->title, r.x + 16 + 2*margin, margin, 0xFFFFFF);
    } else {
        snow_draw_string(fb, tb->title,
            r.x + margin, r.y + margin, 0xFFFFFF);
    }

    snow_draw_border(fb, r.x, r.y, r.w, r.h, 0x404040);
}

void titlebar_on_free(titlebar_t* tb) {
    if (tb->icon) {
        free(tb->icon);
    }

    free(tb->title);
}

void titlebar_on_mouse_entered(titlebar_t* tb, point_t p) {
    (void) p;

    tb->hovered = true;
}

void titlebar_on_mouse_exited(titlebar_t* tb) {
    tb->hovered = false;
}

titlebar_t* titlebar_new(const char* title, const uint8_t* icon) {
    titlebar_t* tb = zalloc(sizeof(titlebar_t));

    tb->widget.flags = UI_EXPAND_HORIZONTAL;
    tb->widget.bounds.h = WM_TB_HEIGHT;
    tb->widget.on_draw = (widget_draw_t) titlebar_on_draw;
    tb->widget.on_free = (widget_freed_t) titlebar_on_free;
    tb->widget.on_mouse_enter = (widget_mouse_entered_t) titlebar_on_mouse_entered;
    tb->widget.on_mouse_exit = (widget_mouse_exited_t) titlebar_on_mouse_exited;
    tb->title = strdup(title);

    if (icon) {
        tb->icon = malloc(UI_ICON_SIZE);
        memcpy(tb->icon, icon, UI_ICON_SIZE);
    }

    return tb;
}

void titlebar_set_title(titlebar_t* tb, const char* title) {
    if (tb->title) {
        free(tb->title);
    }

    tb->title = strdup(title);
}
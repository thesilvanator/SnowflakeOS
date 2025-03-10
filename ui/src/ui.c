#include <ui.h>
#include <snow.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>

/* Is that point in that rect?
 * Note: see `ui_get_absolute_bounds` and friends if coordinate conversion is
 * needed.
 */
bool point_in_rect(point_t p, rect_t r) {
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

/* Returns the color scheme of the widget or of the closest parent.
 */
color_scheme_t* ui_get_color_scheme(widget_t* widget) {
    if (!widget) {
        return &default_color_scheme;
    } else if (widget->color != NULL) {
        return widget->color;
    } else {
        return ui_get_color_scheme(widget->parent);
    }
}

/* Returns a version of the color `c` that is `percent_shift` lighter.
 * Note that `percent_shift` can be in the range (-100;100).
 */
uint32_t ui_shade_color(uint32_t c, int32_t percent_shift) {
    uint32_t r = (c & 0xFF0000) >> 16;
    uint32_t g = (c & 0x00FF00) >> 8;
    uint32_t b = c & 0x0000FF;

    r = clamp((r * (100 + percent_shift) / 100), 0, 255);
    g = clamp((g * (100 + percent_shift) / 100), 0, 255);
    b = clamp((b * (100 + percent_shift) / 100), 0, 255);

    return (r << 16) | (g << 8) | b;
}

/* Sets the widget to be displayed below the titlebar of an app created with
 * `ui_app_new`. Usually, this will be a container such as a `vbox_t` or `hbox_t`.
 */
void ui_set_root(ui_app_t app, widget_t* widget) {
    vbox_t* root = (vbox_t*) app.root;

    vbox_add(root, widget);
}

/* Opens a window with the specified title, dimensions and optional icon.
 * Creates a basic widget hierarchy: a root vbox, containing a titlebar,
 * and whose (initially inexistent) second element is the content of the
 * window.
 * The content of the window must be set later with `ui_set_root`.
 * Note: `icon`, if not NULL, must point to an RGB array of size 16x16px,
 * 3 bytes per pixel.
 */
ui_app_t ui_app_new(const char* title, uint32_t width, uint32_t height, const uint8_t* icon) {
    titlebar_t* tb = titlebar_new(title, icon);
    window_t* win = snow_open_window(title, width, height + W(tb)->bounds.h, WM_NORMAL);

    vbox_t* vbox = vbox_new();
    vbox->widget.bounds = (rect_t) {0, 0, win->fb.width, win->fb.height};

    vbox_add(vbox, W(tb));

    return (ui_app_t) {win, W(vbox)};
}

/* Frees the resources associated with the app, and closes the corresponding
 * window.
 */
void ui_app_destroy(ui_app_t app) {
    if (app.root->on_free) {
        app.root->on_free(app.root);
    }

    snow_close_window(app.win);
}

/* Redraw the app and refresh the window. Usually called in the main loop of
 * a program.
 * TODO: refresh only elements that have changed.
 */
void ui_draw(ui_app_t app) {
    app.root->on_draw(app.root, app.win->fb);
    snow_render_window(app.win);
}

/* Sets the window title.
 */
void ui_set_title(ui_app_t app, const char* title) {
    vbox_t* vb = (vbox_t*) app.root;
    titlebar_t* tb = list_first_entry(&vb->children, titlebar_t);
    titlebar_set_title(tb, title);
}

/* Updates the UI according to the event passed in. Must be called in the
 * application's main loop, fed by events obtained through a call to
 * `snow_get_event`. Or by fake events, whatever.
 */
void ui_handle_input(ui_app_t app, wm_event_t event) {
    // Will be valid in all events we care for
    point_t pos = { event.mouse.position.left, event.mouse.position.top };

    switch (event.type) {
    case WM_EVENT_MOUSE_PRESS: {
        if (app.root->on_click) {
            app.root->on_click(app.root, pos);
        }
    } break;
    case WM_EVENT_MOUSE_RELEASE: {
        if (app.root->on_mouse_release) {
            app.root->on_mouse_release(app.root, pos);
        }
    } break;
    case WM_EVENT_MOUSE_MOVE: {
        if (app.root->on_mouse_move) {
            app.root->on_mouse_move(app.root, pos);
        }
    } break;
    case WM_EVENT_MOUSE_ENTER: {
        if (app.root->on_mouse_enter) {
            app.root->on_mouse_enter(app.root, pos);
        }
    } break;
    case WM_EVENT_MOUSE_EXIT: {
        if (app.root->on_mouse_exit) {
            app.root->on_mouse_exit(app.root);
        }
    } break;
    default:
        break;
    }
}

/* Get a widget's bounds in absolute coordinates, i.e. relative to the window's
 * bounds.
 * Note: the widget must be a descendant of a root widget.
 */
rect_t ui_get_absolute_bounds(widget_t* widget) {
    rect_t r = widget->bounds;

    for (widget_t* p = widget->parent; p != NULL; p = p->parent) {
        r.x += p->bounds.x;
        r.y += p->bounds.y;
    }

    return r;
}

/* Converts a point from `widget`'s parent coordinates to `widget`'s coordinate
 * system.
 */
point_t ui_to_child_local(widget_t* widget, point_t point) {
    return (point_t) {
        .x = point.x - widget->bounds.x,
        .y = point.y - widget->bounds.y
    };
}

/* Converts a point in absolute coordinates, i.e. relative to the window's bounds,
 * to coordinates local to the given widget.
 * Note: works even if the point isn't within the widget's bounds.
 */
point_t ui_absolute_to_local(widget_t* widget, point_t point) {
    if (!widget->parent) {
        return point;
    }

    point_t p = ui_to_child_local(widget, point);

    return ui_absolute_to_local(widget->parent, p);
}
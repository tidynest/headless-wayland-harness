// Persistent virtual-pointer driver for headless wlroots GUI testing.
//
// A headless compositor seat has no input devices, so it advertises no pointer
// capability and Wayland clients (winit/Slint, GTK, Qt, …) never bind a
// wl_pointer — injected clicks/scrolls are silently dropped. This program
// creates ONE zwlr_virtual_pointer_v1 and holds it open for its whole lifetime,
// which keeps the seat's pointer capability present so the client binds the
// pointer. It then executes line commands read from a FIFO on that same pointer:
//
//   move <x> <y>     absolute motion within the <W>x<H> output
//   scroll <dy>      vertical wheel (positive = scroll down / content up)
//   click            left button press + release
//   down / up        left button press or release alone, so moves in between drag
//
// The FIFO is opened O_RDWR so it never reports EOF when a writer disconnects,
// letting the process block on reads and stay alive indefinitely.
//
// Usage: vp <fifo-path> [width] [height]   (defaults 1280x800)
#include <wayland-client.h>
#include <linux/input-event-codes.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vp-client.h"

static struct zwlr_virtual_pointer_manager_v1 *mgr;
static struct wl_seat *seat;

static void g_add(void *d, struct wl_registry *r, uint32_t n,
                  const char *i, uint32_t v) {
    if (!strcmp(i, zwlr_virtual_pointer_manager_v1_interface.name))
        mgr = wl_registry_bind(r, n, &zwlr_virtual_pointer_manager_v1_interface, 1);
    else if (!strcmp(i, wl_seat_interface.name))
        seat = wl_registry_bind(r, n, &wl_seat_interface, 3);
}
static void g_rm(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_l = { g_add, g_rm };

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: vp <fifo> [w] [h]\n"); return 64; }
    const char *fifo = argv[1];
    int W = argc > 2 ? atoi(argv[2]) : 1280;
    int H = argc > 3 ? atoi(argv[3]) : 800;

    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "vp: cannot connect to WAYLAND_DISPLAY\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_l, NULL);
    wl_display_roundtrip(dpy);
    if (!mgr) { fprintf(stderr, "vp: compositor lacks zwlr_virtual_pointer_manager_v1\n"); return 2; }

    struct zwlr_virtual_pointer_v1 *vp =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, seat);
    zwlr_virtual_pointer_v1_motion_absolute(vp, 0, W / 2, H / 2, W, H);
    zwlr_virtual_pointer_v1_frame(vp);
    wl_display_flush(dpy);

    int fd = open(fifo, O_RDWR);                 // O_RDWR: never hits EOF
    if (fd < 0) { perror("vp: open fifo"); return 3; }
    FILE *f = fdopen(fd, "r");
    char line[256];
    uint32_t t = 0;
    while (fgets(line, sizeof line, f)) {
        int a, b;
        if (sscanf(line, "move %d %d", &a, &b) == 2) {
            zwlr_virtual_pointer_v1_motion_absolute(vp, t += 10, a, b, W, H);
            zwlr_virtual_pointer_v1_frame(vp);
        } else if (sscanf(line, "scroll %d", &a) == 1) {
            zwlr_virtual_pointer_v1_axis_source(vp, 0 /* wheel */);
            zwlr_virtual_pointer_v1_axis(vp, t += 10, 0 /* vertical */,
                                         wl_fixed_from_int(a));
            zwlr_virtual_pointer_v1_frame(vp);
        } else if (!strncmp(line, "down", 4) || !strncmp(line, "up", 2)) {
            zwlr_virtual_pointer_v1_button(vp, t += 10, BTN_LEFT, line[0] == 'd');
            zwlr_virtual_pointer_v1_frame(vp);
        } else if (!strncmp(line, "click", 5)) {
            zwlr_virtual_pointer_v1_button(vp, t += 10, BTN_LEFT, 1);
            zwlr_virtual_pointer_v1_frame(vp);
            zwlr_virtual_pointer_v1_button(vp, t += 10, BTN_LEFT, 0);
            zwlr_virtual_pointer_v1_frame(vp);
        }
        wl_display_flush(dpy);
    }
    return 0;
}

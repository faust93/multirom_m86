/*
 * This file is part of MultiROM.
 *
 * MultiROM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiROM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiROM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <string.h>
#include <linux/fb.h>

#include "framebuffer.h"
#include "log.h"
#include "util.h"

// only double-buffering is implemented, this define is just
// for the code to know how many buffers we use
#define NUM_BUFFERS 2

unsigned int smem_len;

struct fb_generic_data {
    px_type *mapped[NUM_BUFFERS];
    int active_buff;
};

static int impl_open(struct framebuffer *fb)
{
    fb->vi.bits_per_pixel = 32;
    INFO("Pixel format: %dx%d @ %dbpp\n", fb->vi.xres, fb->vi.yres, fb->vi.bits_per_pixel);

    fb->vi.vmode = FB_VMODE_NONINTERLACED;
    fb->vi.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
    ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi);

    ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fi);
    ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vi);

    INFO("fb0 reports (possibly inaccurate):\n"
           "  vi.bits_per_pixel = %d\n"
           "  vi.red.offset   = %3d   .length = %3d\n"
           "  vi.green.offset = %3d   .length = %3d\n"
           "  vi.blue.offset  = %3d   .length = %3d\n"
           "  vi.xres = %3d vi.yres = %3d fi.line_length = %3d\n",

           fb->vi.bits_per_pixel,
           fb->vi.red.offset, fb->vi.red.length,
           fb->vi.green.offset, fb->vi.green.length,
           fb->vi.blue.offset, fb->vi.blue.length, fb->vi.xres, fb->vi.yres, fb->fi.line_length);

    smem_len = fb->vi.yres * fb->fi.line_length;
    INFO("smem_len: %d\n",smem_len);

    // mmap and memset to 0 before setting the vi to prevent screen flickering during init
    px_type *mapped = mmap(0, smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);

    if (mapped == MAP_FAILED)
        return -1;

    memset(mapped, 0, smem_len);
    munmap(mapped, smem_len);

    if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi) < 0)
    {
        ERROR("failed to set fb0 vi info");
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fi) < 0)
        return -1;

    mapped = mmap(0, smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);

    if (mapped == MAP_FAILED)
        return -1;

    struct fb_generic_data *data = mzalloc(sizeof(struct fb_generic_data));
    data->mapped[0] = mapped;
    data->mapped[1] = (px_type*) calloc(fb->vi.yres * fb->fi.line_length, 1);

    fb->impl_data = data;

#ifdef TW_SCREEN_BLANK_ON_BOOT
    ioctl(fb->fd, FBIOBLANK, FB_BLANK_POWERDOWN);
    ioctl(fb->fd, FBIOBLANK, FB_BLANK_UNBLANK);
#endif

    return 0;
}

static void impl_close(struct framebuffer *fb)
{
    struct fb_generic_data *data = fb->impl_data;
    __u32 dummy = 0;

    if(data)
    {
        memset(data->mapped[0], 0, smem_len);
        ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->vi);

        munmap(data->mapped[0], smem_len);
        munmap(data->mapped[1], smem_len);
        free(data);
        fb->impl_data = NULL;
    }
}

static int impl_update(struct framebuffer *fb)
{
    struct fb_generic_data *data = fb->impl_data;
    __u32 dummy = 0;

    ioctl(fb->fd, FBIO_WAITFORVSYNC, &dummy);
    memcpy(data->mapped[0], data->mapped[1], fb->vi.yres * fb->fi.line_length);
    ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->vi);

    return 0;
}

static void *impl_get_frame_dest(struct framebuffer *fb)
{
    struct fb_generic_data *data = fb->impl_data;
    return data->mapped[1];
}

const struct fb_impl fb_impl_generic = {
    .name = "Generic",
    .impl_id = FB_IMPL_GENERIC,

    .open = impl_open,
    .close = impl_close,
    .update = impl_update,
    .get_frame_dest = impl_get_frame_dest,
};

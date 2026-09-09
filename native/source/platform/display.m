#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "display.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep Dolphin's BOOL typedef out of Objective-C headers. */
extern void NativePADHandleKeyCode(unsigned short key_code, int pressed,
                                   int repeat);

static NSWindow* s_window;
static int s_headless;
static int s_options_initialized;
static char* s_frame_output_path;
static uint32_t s_frame_output_retrace = 1;
static int s_frame_output_has_retrace;
static int s_frame_output_written;
static uint32_t s_retrace_count;

static void initialize_options(void)
{
    if (s_options_initialized) {
        return;
    }
    s_options_initialized = 1;

    const char* headless = getenv("MELEE_HEADLESS");
    s_headless = headless != NULL && headless[0] != '\0' &&
                 strcmp(headless, "0") != 0;

    const char* output = getenv("MELEE_FRAME_OUTPUT");
    if (output == NULL || output[0] == '\0') {
        return;
    }

    char* value = strdup(output);
    if (value == NULL) {
        return;
    }
    char* path = value;
    char* suffix = strrchr(value, '@');
    if (suffix != NULL && suffix[1] != '\0') {
        char* end = NULL;
        unsigned long retrace = strtoul(suffix + 1, &end, 10);
        if (*end == '\0' && retrace <= UINT32_MAX) {
            *suffix = '\0';
            s_frame_output_retrace = (uint32_t) retrace;
            s_frame_output_has_retrace = 1;
        }
    } else {
        char* colon = strchr(value, ':');
        if (colon != NULL && colon != value) {
            int digits = 1;
            for (char* p = value; p < colon; ++p) {
                if (!isdigit((unsigned char) *p)) {
                    digits = 0;
                    break;
                }
            }
            if (digits) {
                char* end = NULL;
                unsigned long retrace = strtoul(value, &end, 10);
                if (end == colon && retrace <= UINT32_MAX && colon[1] != '\0') {
                    *colon = '\0';
                    s_frame_output_retrace = (uint32_t) retrace;
                    s_frame_output_has_retrace = 1;
                    path = colon + 1;
                    memmove(value, path, strlen(path) + 1);
                }
            }
        }
    }
    if (value[0] != '\0') {
        s_frame_output_path = value;
    } else {
        free(value);
    }
}

static void release_pixels(void* info, const void* data, size_t size)
{
    (void) info;
    (void) size;
    free((void*) data);
}

static void pump_events(void)
{
    if (s_headless) {
        return;
    }
    NSEvent* event;
    do {
        event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:[NSDate distantPast]
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
        if (event != nil) {
            if ([event type] == NSEventTypeKeyDown ||
                [event type] == NSEventTypeKeyUp) {
                NativePADHandleKeyCode((uint16_t) [event keyCode],
                                       [event type] == NSEventTypeKeyDown,
                                       [event isARepeat]);
            }
            [NSApp sendEvent:event];
        }
    } while (event != nil);
}

void NativeDisplaySetRetraceCount(uint32_t retrace_count)
{
    s_retrace_count = retrace_count;
}

void NativeDisplayPumpEvents(void)
{
    initialize_options();
    if (s_headless) {
        return;
    }
    @autoreleasepool {
        pump_events();
    }
}

static void ensure_window(uint16_t width, uint16_t height)
{
    if (s_headless || s_window != nil) {
        return;
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    NSRect frame = NSMakeRect(0, 0, width, height);
    s_window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [s_window setTitle:@"Melee native ARM64"];
    [s_window setReleasedWhenClosed:NO];
    [s_window center];
    [s_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    NSView* view = [s_window contentView];
    [view setWantsLayer:YES];
    [view layer].contentsGravity = kCAGravityResizeAspect;
}

static void write_frame_ppm(const char* path, const void* xfb, uint16_t width,
                            uint16_t height, uint16_t stride_pixels)
{
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return;
    }
    fprintf(file, "P6\n%u %u\n255\n", width, height);
    const uint8_t* source = (const uint8_t*) xfb;
    for (uint16_t y = 0; y < height; y++) {
        const uint8_t* row = source + (size_t) y * stride_pixels * 2;
        for (uint16_t x = 0; x < width; x++) {
            uint16_t value = ((uint16_t) row[x * 2] << 8) | row[x * 2 + 1];
            uint8_t rgb[3] = {
                (uint8_t) ((((value >> 11) & 0x1f) * 255 + 15) / 31),
                (uint8_t) ((((value >> 5) & 0x3f) * 255 + 31) / 63),
                (uint8_t) (((value & 0x1f) * 255 + 15) / 31),
            };
            fwrite(rgb, sizeof(rgb), 1, file);
        }
    }
    fclose(file);
    s_frame_output_written = 1;
}

void NativeDisplayPresent(const void* xfb, uint16_t width, uint16_t height,
                          uint16_t stride_pixels)
{
    initialize_options();
    if (xfb == NULL || width == 0 || height == 0) {
        return;
    }
    if (stride_pixels < width) {
        stride_pixels = width;
    }

    if (s_frame_output_path != NULL && !s_frame_output_written &&
        (!s_frame_output_has_retrace || s_retrace_count >= s_frame_output_retrace)) {
        write_frame_ppm(s_frame_output_path, xfb, width, height, stride_pixels);
    }

    if (s_headless) {
        return;
    }

    @autoreleasepool {
        ensure_window(width, height);
        if (s_window == nil) {
            return;
        }

        size_t pixel_count = (size_t) width * height;
        uint32_t* pixels = malloc(pixel_count * sizeof(*pixels));
        if (pixels == NULL) {
            return;
        }

        const uint8_t* source = (const uint8_t*) xfb;
        for (uint16_t y = 0; y < height; y++) {
            const uint8_t* row = source + (size_t) y * stride_pixels * 2;
            for (uint16_t x = 0; x < width; x++) {
                uint16_t value = ((uint16_t) row[x * 2] << 8) | row[x * 2 + 1];
                uint8_t r = (uint8_t) ((((value >> 11) & 0x1f) * 255 + 15) / 31);
                uint8_t g = (uint8_t) ((((value >> 5) & 0x3f) * 255 + 31) / 63);
                uint8_t b = (uint8_t) (((value & 0x1f) * 255 + 15) / 31);
                pixels[(size_t) y * width + x] =
                    0xff000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
            }
        }

        CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
        CGDataProviderRef provider = CGDataProviderCreateWithData(
            NULL, pixels, pixel_count * sizeof(*pixels), release_pixels);
        CGImageRef image = CGImageCreate(
            width, height, 8, 32, (size_t) width * sizeof(*pixels), color_space,
            kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, provider,
            NULL, false, kCGRenderingIntentDefault);
        if (image != NULL) {
            [s_window contentView].layer.contents = (__bridge id) image;
            CGImageRelease(image);
        }
        if (provider != NULL) {
            CGDataProviderRelease(provider);
        }
        if (color_space != NULL) {
            CGColorSpaceRelease(color_space);
        }
        [s_window displayIfNeeded];
        pump_events();
    }
}

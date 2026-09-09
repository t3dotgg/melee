#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "display.h"

#include <stdlib.h>

static NSWindow* s_window;

static void release_pixels(void* info, const void* data, size_t size)
{
    (void) info;
    (void) size;
    free((void*) data);
}

static void pump_events(void)
{
    NSEvent* event;
    do {
        event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:[NSDate distantPast]
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
        if (event != nil) {
            [NSApp sendEvent:event];
        }
    } while (event != nil);
}

static void ensure_window(uint16_t width, uint16_t height)
{
    if (s_window != nil) {
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

void NativeDisplayPresent(const void* xfb, uint16_t width, uint16_t height,
                          uint16_t stride_pixels)
{
    if (xfb == NULL || width == 0 || height == 0) {
        return;
    }
    if (stride_pixels < width) {
        stride_pixels = width;
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

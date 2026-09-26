#include "macos_media_keys.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <IOKit/hidsystem/ev_keymap.h>

#include <cstdio>
#include <utility>

namespace {

std::function<void()> g_onPlayPause;
CFMachPortRef g_eventTap = nullptr;
CFRunLoopSourceRef g_runLoopSource = nullptr;

// Media keys arrive disguised as NSSystemDefined events rather than
// ordinary key-downs -- this decodes the key code/state out of the
// event's data1 field the same way every other media-key-tap
// implementation on macOS does (there's no public, documented API for
// this; ev_keymap.h's NX_KEYTYPE_*/NX_KEYDOWN/NX_KEYUP constants and this
// bit layout are the de facto standard, unchanged across macOS releases
// for well over a decade).
//
// Returning nullptr for the Play/Pause key (both its key-down and its
// key-up) discards the event instead of just observing it, so it doesn't
// also reach whatever else on the system would otherwise react to it
// (Apple Music, another player, etc.) -- this app becomes *the* handler
// for that key while it's running. Every other media key (next/previous/
// fast-forward/rewind) is passed through unmodified since this app has no
// behavior for them.
CGEventRef handleEvent(CGEventTapProxy /*proxy*/, CGEventType type, CGEventRef event, void* /*refcon*/) {
    if (type == static_cast<CGEventType>(NX_SYSDEFINED)) {
        NSEvent* nsEvent = [NSEvent eventWithCGEvent:event];
        if (nsEvent.type == NSEventTypeSystemDefined && nsEvent.subtype == 8) {
            const int data = static_cast<int>(nsEvent.data1);
            const int keyCode = (data & 0xFFFF0000) >> 16;
            const int keyFlags = data & 0xFFFF;
            // The down/up state lives in bits 8-15 of keyFlags, not bits
            // 0-7 -- (keyFlags & 0xFF) == NX_KEYDOWN (a mistake that's
            // floating around in a few online copies of this snippet)
            // checks the wrong byte, so it's essentially never true; that
            // silently broke the play/pause callback while still letting
            // the keyCode-based blocking below work, since that's read
            // from a different, unaffected byte range (bits 16-31).
            const bool isKeyDown = ((keyFlags & 0xFF00) >> 8) == NX_KEYDOWN;
            if (keyCode == NX_KEYTYPE_PLAY) {
                if (isKeyDown && g_onPlayPause) {
                    g_onPlayPause();
                }
                return nullptr;
            }
        }
    }
    return event;
}

}  // namespace

namespace macos_media_keys {

void install(std::function<void()> onPlayPause) {
    g_onPlayPause = std::move(onPlayPause);

    const CGEventMask mask = CGEventMaskBit(NX_SYSDEFINED);
    // kCGHIDEventTap: taps the event as close to the hardware as possible,
    // before any app (including this one's own GLFW/Cocoa event loop).
    // kCGEventTapOptionDefault (rather than ListenOnly) is required for
    // handleEvent()'s `return nullptr` to actually discard the Play/Pause
    // key instead of merely being notified of it.
    g_eventTap = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, mask,
                                   &handleEvent, nullptr);
    if (!g_eventTap) {
        std::fprintf(stderr,
                      "Media keys: couldn't install the event tap (Play/Pause key won't be hooked up). "
                      "Grant this app Accessibility/Input Monitoring access in System Settings > "
                      "Privacy & Security, then restart it, if you want media-key support.\n");
        return;
    }

    // Added to the *main* thread's run loop, not run via our own
    // CFRunLoopRun() -- that would block forever, and this app already has
    // its own loop driven by glfwPollEvents(). GLFW's Cocoa backend pumps
    // NSApplication's event queue every frame, which services the main
    // run loop's common modes along the way, so this source gets a chance
    // to fire without any changes to that loop.
    g_runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, g_eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), g_runLoopSource, kCFRunLoopCommonModes);
    CGEventTapEnable(g_eventTap, true);
}

void shutdown() {
    if (g_eventTap) {
        CGEventTapEnable(g_eventTap, false);
    }
    if (g_runLoopSource) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), g_runLoopSource, kCFRunLoopCommonModes);
        CFRelease(g_runLoopSource);
        g_runLoopSource = nullptr;
    }
    if (g_eventTap) {
        CFRelease(g_eventTap);
        g_eventTap = nullptr;
    }
    g_onPlayPause = nullptr;
}

}  // namespace macos_media_keys

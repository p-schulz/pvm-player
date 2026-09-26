#include "macos_sleep_guard.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <cstdio>

namespace {

IOPMAssertionID g_assertionId = 0;
bool g_active = false;

}  // namespace

namespace macos_sleep_guard {

void setActive(bool active) {
    if (active == g_active) {
        return;
    }
    g_active = active;

    if (active) {
        // PreventUserIdleDisplaySleep: blocks the *display* idle-sleep timer
        // specifically (what actually blanks the screen), while still
        // letting the system itself sleep if the lid is closed or the power
        // button is pressed -- this app has no business overriding those.
        const IOReturn result = IOPMAssertionCreateWithName(
            kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn,
            CFSTR("PVM Player: media playback"), &g_assertionId);
        if (result != kIOReturnSuccess) {
            std::fprintf(stderr, "[sleep] could not create display-sleep assertion (0x%x)\n", result);
            g_assertionId = 0;
        }
    } else if (g_assertionId != 0) {
        IOPMAssertionRelease(g_assertionId);
        g_assertionId = 0;
    }
}

void shutdown() {
    setActive(false);
}

}  // namespace macos_sleep_guard

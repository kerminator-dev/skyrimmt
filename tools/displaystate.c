/* Public fallback for the launcher's Fit screen mode. The private benchmark
 * helper has additional power/lock reporting; launching only needs w/h. */
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
int main(void) {
    CGDirectDisplayID display = CGMainDisplayID();
    CGRect bounds = CGDisplayBounds(display);
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    double hz = mode ? CGDisplayModeGetRefreshRate(mode) : 0;
    if (mode) CGDisplayModeRelease(mode);
    if (bounds.size.width <= 0 || bounds.size.height <= 0) return 1;
    printf("hz=%.0f w=%.0f h=%.0f\n", hz > 0 ? hz : 60, bounds.size.width, bounds.size.height);
    return 0;
}

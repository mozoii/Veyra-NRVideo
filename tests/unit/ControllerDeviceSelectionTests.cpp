// Acquisition preference: a stored SDL GUID wins, a missing GUID keeps the
// original first-pad behaviour, and an empty list never selects a device.
#include "veyra/input/ControllerInput.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
} // namespace

int wmain() {
    using veyra::input::preferredDeviceIndex;
    check(preferredDeviceIndex({}, "") == -1, "no devices yields no selection");
    check(preferredDeviceIndex({}, "guid") == -1, "a preference cannot select an absent device");
    check(preferredDeviceIndex({"a", "b"}, "") == 0, "Auto keeps the first-pad behaviour");
    check(preferredDeviceIndex({"a", "b"}, "b") == 1, "a preferred GUID selects its device");
    check(preferredDeviceIndex({"a", "b"}, "missing") == 0, "a missing GUID falls back to the first pad");
    check(preferredDeviceIndex({"b"}, "b") == 0, "a single preferred device is selected");
    return failures ? 1 : 0;
}

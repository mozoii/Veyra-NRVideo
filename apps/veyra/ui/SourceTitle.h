#pragma once

#include <filesystem>
#include <string>

namespace veyra::ui {

// Physical capture sources are opened through opaque connection strings
// ("capture:<index>..." and "capture2:<hex device path>:..."). They are not
// file names. Using them as the window title leaked a multi-hundred character
// hex blob to every window list (windows list of third-party broadcast tools,
// taskbar, Alt+Tab), which made the live window impossible to recognise and
// matched the reported "cannot find Veyra while it is capturing" symptom.
inline std::wstring windowTitleForSource(const std::wstring& source) {
    if (source.empty()) return L"Veyra — local experimental build";
    return L"Veyra — " + std::filesystem::path(source).filename().wstring();
}

} // namespace veyra::ui

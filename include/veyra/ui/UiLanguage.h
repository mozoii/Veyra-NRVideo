#pragma once
// Interface language (Settings → Interface language). English is the source language:
// every user-visible string in QML (qsTr) and C++ (tr) is written in it and is
// itself the lookup key. i18n/catalog.json, compiled into the executable, maps each
// source string to each supported locale (en is the source column); a missing entry
// shows the English source.
//
// Logs stay in the source language on purpose: field logs are read by the developers.
#include <QString>
#include <string>

namespace veyra::ui::i18n {

// An explicit supported locale wins. Otherwise use the system language, falling back to English.
QString resolve(const QString& preference);
// Installs the translator for `code` on the application. English (the source
// language) removes it, leaving the source text in place.
// Returns false when the catalog has no column for it; the source text then shows.
bool apply(const QString& code);
QString current();

// Text that did not pass through tr()/qsTr(): engine status and error messages and
// fixed tables. Exact catalog match first, then the catalog's message patterns
// (std::format "{}" slots and "…: " prefixes), then piece by piece across " · ",
// "; " and "\n". Text with no match comes back unchanged.
QString text(const QString& source);
QString text(const std::wstring& source);

} // namespace veyra::ui::i18n

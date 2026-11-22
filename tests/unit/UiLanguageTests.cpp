// Interface language runtime (UiLanguage.h) against the real catalog.
// The source language is English: the catalog keys on English source strings.
#include "veyra/ui/UiLanguage.h"

#include <QCoreApplication>
#include <QLocale>
#include <cstdio>

namespace {
int failures = 0;
void expect(const QString& got, const QString& want, const char* what) {
    if (got == want) return;
    ++failures;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.toUtf8().constData(), want.toUtf8().constData());
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using namespace veyra::ui;

    const struct { const char* code; const char* action; } languages[] = {
        {"es", "Cerrar"}, {"fr", "Fermer"}, {"de", "Schließen"},
        {"pt-BR", "Fechar"}, {"ru", "Закрыть"}
    };
    for (const auto& language : languages) {
        const auto code = QString::fromLatin1(language.code);
        expect(i18n::resolve(code), code, "explicit language");
        if (!i18n::apply(code)) return 1;
        expect(QCoreApplication::translate("Player", "Close"), QString::fromUtf8(language.action), "localized close action");
    }

    expect(i18n::resolve(QStringLiteral("ja")), QStringLiteral("ja"), "explicit choice kept");

    // Source language: no translator is installed; text passes through unchanged.
    if (!i18n::apply(QStringLiteral("en"))) { std::printf("FAIL catalog did not load\n"); return 1; }
    expect(QCoreApplication::translate("Any", "Export finished"), QStringLiteral("Export finished"), "source language");
    expect(QCoreApplication::translate("Any", "Off", "off"), QStringLiteral("Off"), "source language disambiguation");
    expect(QCoreApplication::translate("Any", "Close"), QStringLiteral("Close"), "source language plain next to a disambiguated entry");
    expect(i18n::text(QStringLiteral("Connection lost (error 42). Check the network and the host.")),
           QStringLiteral("Connection lost (error 42). Check the network and the host."), "source language leaves unmatched text");

    // Exact entry, through Qt's own translate() as qsTr()/tr() use it.
    if (!i18n::apply(QStringLiteral("es"))) return 1;
    expect(QCoreApplication::translate("Any", "Export finished"), QStringLiteral("Exportación finalizada"), "exact via translator");
    // Disambiguated entry, and the plain one beside it.
    expect(QCoreApplication::translate("Any", "Off", "off"), QStringLiteral("Desactivado"), "disambiguation");
    expect(QCoreApplication::translate("Any", "Close"), QStringLiteral("Cerrar"), "plain entry next to a disambiguated one");

    // std::format slots carried across a pattern.
    expect(i18n::text(QStringLiteral("Connection lost (error 42). Check the network and the host.")),
           QStringLiteral("Conexión perdida (error 42). Revisa la red y el host."), "format pattern");
    // A message built from pieces joined by "; ".
    expect(i18n::text(QStringLiteral("HDR video automatically uses HEVC Main10; Video export finished")),
           QStringLiteral("HDR video automáticamente HEVC Main10; Exportación de vídeo terminada"), "segments");
    // A capture format label with a technical tag at the end.
    expect(i18n::text(QStringLiteral("1920 x 1080 @ 120.00 fps · NV12 · Native · low latency [format 16]")),
           QStringLiteral("1920 x 1080 @ 120.00 fps · NV12 · Nativo · baja latencia [format 16]"), "tagged segment");
    // The presentation status (VideoPresenter) joins its pieces with " · "; each is its own entry.
    expect(i18n::text(QStringLiteral("NVIDIA Low Latency: Off · Display sync: auto · no tearing and no VSync wait (lowest latency)")),
           QStringLiteral("NVIDIA Low Latency: Off · Sincronización de pantalla: auto · sin lágrimas y sin espera VSync (latencia más baja)"), "presentation status pieces");
    expect(i18n::text(QStringLiteral("NVIDIA Low Latency unavailable; using low queue depth (see the log) · Output cap: off · VSync: 1-frame display queue, waits for refresh before reading a frame · Display sync: VSync")),
           QStringLiteral("NVIDIA Latencia baja no disponible; usando baja profundidad de cola (ver el registro) · Gorra de salida: fuera · VSync: cola de pantalla de 1 marco, espera para refrescar antes de leer un marco · Sincronización de pantalla: VSync"),
           "presentation fallback status pieces");
    // Unknown text and paths come back unchanged.
    expect(i18n::text(QStringLiteral("D:/videos/my movie.mkv")), QStringLiteral("D:/videos/my movie.mkv"), "path untouched");

    // Korean: explicit resolve and the catalog columns.
    expect(i18n::resolve(QStringLiteral("ko")), QStringLiteral("ko"), "explicit Korean choice kept");
    if (QLocale::system().language() == QLocale::Korean)
        expect(i18n::resolve(QStringLiteral("auto")), QStringLiteral("ko"), "Korean system language detected");
    if (!i18n::apply(QStringLiteral("ko"))) { std::printf("FAIL Korean catalog did not load\n"); return 1; }
    expect(i18n::current(), QStringLiteral("ko"), "Korean language active");
    expect(QCoreApplication::translate("Any", "Export finished"), QStringLiteral("내보내기 완료"), "Korean exact translation");
    expect(QCoreApplication::translate("Any", "Off", "off"), QStringLiteral("끔"), "Korean off disambiguation");
    expect(QCoreApplication::translate("Any", "Close"), QStringLiteral("닫기"), "Korean close disambiguation");
    expect(i18n::text(QStringLiteral("Connection lost (error 42). Check the network and the host.")),
           QStringLiteral("연결이 끊어졌습니다(오류 42). 네트워크와 호스트를 확인하세요."), "Korean format pattern");
    expect(i18n::text(QStringLiteral("HDR video automatically uses HEVC Main10; Video export finished")),
           QStringLiteral("HDR 동영상은 자동으로 HEVC Main10으로 인코딩됩니다; 동영상 내보내기 완료"), "Korean status pieces");
    expect(i18n::text(QStringLiteral("D:/videos/my movie.mkv")), QStringLiteral("D:/videos/my movie.mkv"), "Korean leaves unknown paths untouched");

    // Switching back to the source language shows the English source.
    i18n::apply(QStringLiteral("en"));
    expect(QCoreApplication::translate("Any", "Export finished"), QStringLiteral("Export finished"), "switching back from Korean (source language)");

    // Chinese: a target language; the catalog columns are the translations.
    i18n::apply(QStringLiteral("zh-CN"));
    expect(QCoreApplication::translate("Any", "Export finished"), QStringLiteral("导出完成"), "Chinese translation");
    expect(i18n::text(QStringLiteral("Export finished")), QStringLiteral("导出完成"), "Chinese text()");

    std::printf("ui i18n tests: %d failures\n", failures);
    return failures ? 1 : 0;
}

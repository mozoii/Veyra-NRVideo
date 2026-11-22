import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: root
    objectName: "live-translation-dialog"
    key: "translation"
    title: qsTr("Live translation")
    glyph: "type"
    dialogWidth: 580
    actions: [{ label: qsTr("Done"), primary: true }]
    onActionTriggered: dialogHost.close()

    readonly property var translation: veyra.liveTranslation || null
    readonly property bool supported: translation !== null && translation.supported
    readonly property int provider: translation ? translation.provider : 0
    readonly property var providers: [
        {id: "0", label: qsTr("Local model")},
        {id: "1", label: qsTr("Custom API (OpenAI compatible)")},
        {id: "2", label: "MyMemory"}
    ]
    readonly property var languages: [
        {id: "zh-CN", label: qsTr("简体中文")}, {id: "zh-TW", label: qsTr("繁體中文")},
        {id: "en", label: "English"}, {id: "ja", label: "日本語"}, {id: "ko", label: "한국어"},
        {id: "fr", label: "Français"}, {id: "de", label: "Deutsch"}, {id: "es", label: "Español"},
        {id: "pt-BR", label: "Português (Brasil)"}, {id: "ru", label: "Русский"},
        {id: "it", label: "Italiano"}, {id: "ar", label: "العربية"},
        {id: "th", label: "ไทย"}, {id: "vi", label: "Tiếng Việt"}
    ]
    readonly property var sourceLanguages: provider === 2 ? languages :
        [{id: "", label: qsTr("Detect source language") }].concat(languages)
    function languageLabel(options, value) {
        return options.find(option => option.id === value)?.label || value
    }

    DialogGroup {
        VRow {
            label: qsTr("Show live translation on video")
            hintBelow: true
            hint: root.supported
                ? qsTr("Windows Live Captions transcribes system audio. Translations appear in the Veyra preview.")
                : qsTr("Live translation requires Windows 11 22H2 or later")
            VSwitch {
                objectName: "live-translation-enabled"
                enabled: root.supported
                checked: root.translation ? root.translation.enabled : false
                onToggled: value => { root.translation.enabled = value }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("Speech recognition")
            hint: qsTr("Download the speech pack in Windows on first use and set its caption language to the source language.")
            VButton {
                objectName: "live-translation-windows"
                text: qsTr("Open Windows Live Captions")
                onClicked: root.translation.openWindowsCaptions()
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("Translation engine")
            hintBelow: true
            hint: root.provider === 0
                ? qsTr("Runs on the CPU. Caption text stays on this device.")
                : qsTr("Enabling this sends system caption text to the selected service.")
            VSelect {
                objectName: "live-translation-provider"
                implicitWidth: 224
                options: root.providers
                value: root.providers[root.provider].label
                onPicked: id => { root.translation.provider = Number(id) }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("Source language")
            VSelect {
                options: root.sourceLanguages
                value: root.translation ? root.languageLabel(options, root.translation.sourceLanguage) : ""
                onPicked: id => { root.translation.sourceLanguage = id }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("Translate to")
            VSelect {
                objectName: "live-translation-target"
                options: root.languages
                value: root.translation ? root.languageLabel(options, root.translation.targetLanguage) : ""
                onPicked: id => { root.translation.targetLanguage = id }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("Show original text")
            VSwitch {
                checked: root.translation ? root.translation.showOriginal : true
                onToggled: value => { root.translation.showOriginal = value }
            }
        }
    }

    DialogGroup {
        visible: root.supported && root.provider === 0
        VRow {
            label: root.translation && root.translation.modelInstalled ? qsTr("Local model ready") : qsTr("Local model")
            hintBelow: true
            hint: qsTr("Qwen2.5 1.5B Q4 and CPU runtime · %1 GB on demand")
                .arg(root.translation ? (root.translation.downloadBytes / 1000000000).toFixed(2) : "")
            RowLayout {
                VButton {
                    objectName: "live-translation-download"
                    text: root.translation && root.translation.downloading ? qsTr("Cancel download")
                        : root.translation && root.translation.modelInstalled ? qsTr("Verify runtime") : qsTr("Download model")
                    enabled: root.translation !== null && (!root.translation.enabled || root.translation.downloading
                        || !root.translation.modelInstalled)
                    onClicked: {
                        if (root.translation.downloading) root.translation.cancelModelDownload()
                        else root.translation.downloadModel()
                    }
                }
            }
        }
        ProgressBar {
            objectName: "live-translation-progress"
            Layout.fillWidth: true
            visible: root.translation !== null && root.translation.downloading
            value: root.translation ? root.translation.downloadProgress : 0
        }
    }

    DialogGroup {
        visible: root.supported && root.provider === 1
        VRow {
            label: qsTr("API endpoint")
            DialogTextField {
                objectName: "live-translation-api-base"
                implicitWidth: 248
                placeholder: "https://example.com/v1"
                text: root.translation ? root.translation.apiBase : ""
                onTyped: value => { root.translation.apiBase = value }
            }
        }
        VRow {
            label: qsTr("API model name")
            DialogTextField {
                implicitWidth: 248
                text: root.translation ? root.translation.apiModel : ""
                onTyped: value => { root.translation.apiModel = value }
            }
        }
        VRow {
            label: "API Key"
            hint: qsTr("For this session")
            DialogTextField {
                implicitWidth: 248
                echoMode: TextInput.Password
                onTyped: value => { if (root.translation) root.translation.setApiKey(value) }
            }
        }
    }
    Text {
        objectName: "live-translation-status"
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.translation ? root.translation.status : ""
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        wrapMode: Text.Wrap
    }
}

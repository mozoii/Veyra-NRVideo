// Dialog routing and scrim. Each dialog owns its own editable content.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: host
    // Compatibility type used by the professional and node views.
    component VTextField: DialogTextField {}
    anchors.fill: parent
    z: 100

    // Active dialog: network/moonlight/xbox/subtitle/audio/save/manage.
    property string dialog: ""
    property bool onboardingActive: false

    function open(key) {
        if (key === "save") saveDialog.resetForOpen()
        if (key === "manage") {
            manageDialog.mode = veyra.nodeMode === 1 ? "node" : "list"
            manageDialog.feedback = ""
        }
        veyra.logUi("ui-dialog", "open " + key + " (was " + (host.dialog || "none") + ")")
        host.dialog = key
    }
    function close() { host.dialog = "" }

    // The dialogs ask the shell to move to the picture once a source starts.
    signal startMoonlight()
    signal startXbox()

    // Scrim (M16): the design dims the page behind the dialog, opacity .2s linear.
    // The native video window cannot be dimmed from QML; only the dialog panel is cut
    // out of it (videoCover below), so the picture beside the dialog stays undimmed.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        opacity: host.dialog !== "" && !host.onboardingActive ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        TapHandler { onTapped: host.close() }
    }
    // For the motion probe: the open dialog's scale.
    property real motionScale: 1

    // --- shared chrome ----------------------------------------------------

    NetworkSourceDialog {
        id: networkDialog
        dialogHost: host
    }

    // --- PC streaming (Sunshine / GameStream) ------------------------------------
    // Hosts, pairing, the host's apps and the stream-core settings. The effect chain is not here: once
    // connected, the picture goes through the software's own pages like any other source.
    PcSourceDialog {
        id: mlDialog
        dialogHost: host
    }

    // --- Xbox streaming (unofficial) ------------------------------------------
    // Sign in with the Xbox (Microsoft) account on Microsoft's own page via a device code, pick a console,
    // start. The software never sees the password; only a refresh token is kept, encrypted for this user.
    XboxSourceDialog {
        id: xbDialog
        dialogHost: host    }

    // --- Subtitle settings --------------------------------------------------
    SubtitleSettingsDialog {
        id: subtitleDialog
        dialogHost: host
    }
    LiveTranslationDialog {
        dialogHost: host
    }

    // --- Audio settings -----------------------------------------------------
    AudioSettingsDialog {
        dialogHost: host
    }

    // --- Save as preset -----------------------------------------------------
    // The design lists every part that will be saved before asking for a name, so
    // the user can see what they are about to store.
    SavePresetDialog {
        id: saveDialog
        dialogHost: host
    }

    // --- Manage presets -----------------------------------------------------
    ManagePresetsDialog {
        id: manageDialog
        dialogHost: host
    }

    // --- small building blocks the design's dialogs use --------------------

}

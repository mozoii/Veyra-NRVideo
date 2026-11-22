// Professional-view reusable presentation component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

RowLayout {
    property int size: 56
    property bool showLabels: true
    spacing: 10
    ProPerfOrb {
        size: parent.size
        showLabel: parent.showLabels
        known: veyra.chainTotalMsKnown && veyra.stageBudgetMs > 0
        fraction: known ? veyra.chainTotalMs / veyra.stageBudgetMs : 0
        value: veyra.chainTotalMsKnown ? veyra.chainTotalMs.toFixed(1) + "ms" : "—"
        label: qsTr("Processing time")
        tip: qsTr("Total enhancement chain time (last-second average, measured as in 1.4.4); the ring = share of the source frame budget") + (veyra.stageBudgetMs > 0 ? " " + veyra.stageBudgetMs.toFixed(1) + " ms" : "") + qsTr(" share")
    }
    ProPerfOrb {
        size: parent.size
        showLabel: parent.showLabels
        known: veyra.gpuUtilizationKnown
        fraction: veyra.gpuUtilization / 100
        value: known ? Math.round(veyra.gpuUtilization) + "%" : "—"
        label: qsTr("GPU usage")
        tip: qsTr("Windows GPU engine usage (") + (veyra.gpuMonitorName || qsTr("All GPUs")) + qsTr(", busiest engine type, whole system), sampled every second; change the monitored GPU under Settings → General and appearance")
    }
    // How much of one source frame's time the enhancement chain uses (the plan's
    // "load budget = time / budget"). It used to show output fps / target fps, which
    // reads 100% whenever the output keeps up, however light the load.
    ProPerfOrb {
        size: parent.size
        showLabel: parent.showLabels
        known: veyra.chainTotalMsKnown && veyra.stageBudgetMs > 0
        fraction: known ? veyra.chainTotalMs / veyra.stageBudgetMs : 0
        value: known ? Math.round(veyra.chainTotalMs / veyra.stageBudgetMs * 100) + "%" : "—"
        label: qsTr("Load budget")
        tip: qsTr("Enhancement chain GPU time (last-second average) as a share of one source frame (") + veyra.stageBudgetMs.toFixed(1)
             + qsTr(" ms); above 100% it cannot keep up with the source frame rate. Current state: ") + veyra.runStatus
    }
}

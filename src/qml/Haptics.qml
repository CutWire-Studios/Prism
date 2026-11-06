pragma Singleton
import QtQuick

// Stand-in for the platform haptics backend, which is not ported. Every cue is a no-op.
QtObject {
    function press() {
    }

    function select() {
    }

    function confirm() {
    }

    function detent() {
    }

    function boundary() {
    }

    function error() {
    }

    function toggle(on) {
    }

    function pickUp() {
    }

    function drop() {
    }
}

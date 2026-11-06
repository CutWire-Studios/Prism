pragma Singleton
import QtQuick

// App-wide notification queue.
//
// Before this, user-facing failures had nowhere to go: import errors, save
// errors, `subtitleGenerationFinished` and `exportFinished` were never surfaced,
// and `lastMessage` rendered as static muted text in the header with no severity
// and no dismiss. Call Toasts.error(...) / .success(...) from anywhere; the
// visual host lives once in Main.qml.
//
// Only the newest toast is on screen (#289: a stack of them sat on the timeline).
// Everything posted is also kept in `history`, which the header bell lists, so a
// short timeout never loses a message.
QtObject {
    id: toasts

    readonly property ListModel model: ListModel { }
    // Newest first. Session-only.
    readonly property ListModel history: ListModel { }

    // Bumped per post so each entry has a stable identity for removal.
    property int _nextId: 1

    // Errors stay until dismissed — a failure the user missed is worse than a
    // lingering banner. Everything else auto-expires; info/success are one-line
    // confirmations, so they go sooner.
    readonly property int defaultTimeout: 5000
    readonly property int briefTimeout: 3000
    readonly property int maxQueued: 4
    readonly property int maxHistory: 50

    // History entries the user has not seen: posted while the bell list was closed
    // and not dismissed by hand.
    property int unreadCount: 0

    // Bumped on every queue change. ListModel.setProperty notifies no count change, so
    // bindings that depend on which toast is in front read this.
    property int revision: 0

    // The "+N" badge on the toast asks for the full list; each shell's bell opens it.
    signal historyRequested()

    // Callbacks for toasts with an action button, by toastId. A ListModel row can't hold a
    // function, so the row carries only the label. Kept until the history entry goes, so a
    // missed suggestion can still be acted on from the bell.
    property var _actions: ({})

    // actionText/action are optional: a button on the toast that runs action() and dismisses.
    function post(severity, message, timeout, actionText, action) {
        if (!message || message.length === 0)
            return -1
        const hasAction = !!actionText && typeof action === "function"

        // Collapse an identical consecutive message into a repeat count instead
        // of stacking duplicates (transform-blocked fires repeatedly on drag).
        if (model.count > 0) {
            const last = model.get(model.count - 1)
            // Never fold a repeat into a toast that is already animating out.
            if (!hasAction && last.message === message && last.severity === severity
                    && !last.exiting && last.actionText.length === 0) {
                model.setProperty(model.count - 1, "repeats", last.repeats + 1)
                _setHistory(last.toastId, "repeats", last.repeats + 1)
                revision++
                return last.toastId
            }
        }

        // A newer message supersedes passing ones; only what needs an answer (errors,
        // offers) waits underneath. Superseded toasts are still in history.
        for (let i = 0; i < model.count; ++i) {
            const row = model.get(i)
            if (!row.exiting && row.severity !== "error" && row.actionText.length === 0)
                model.setProperty(i, "exiting", true)
        }

        const id = _nextId++
        model.append({
            toastId: id,
            severity: severity,
            message: message,
            repeats: 1,
            exiting: false,
            actionText: hasAction ? actionText : "",
            timeout: timeout !== undefined ? timeout
                     : severity === "error" ? 0
                     : severity === "warning" ? defaultTimeout
                     : briefTimeout
        })
        if (hasAction)
            _actions[id] = action

        history.insert(0, {
            toastId: id,
            severity: severity,
            message: message,
            repeats: 1,
            actionText: hasAction ? actionText : "",
            time: new Date().toLocaleTimeString(Qt.locale(), Locale.ShortFormat),
            unread: true
        })
        unreadCount++
        while (history.count > maxHistory) {
            const old = history.get(history.count - 1)
            if (old.unread)
                unreadCount--
            if (!_isQueued(old.toastId))
                delete _actions[old.toastId]
            history.remove(history.count - 1)
        }

        while (model.count > maxQueued)
            remove(model.get(0).toastId)

        revision++
        return id
    }

    function trigger(toastId) {
        const action = _actions[toastId]
        delete _actions[toastId]
        _setHistory(toastId, "actionText", "")
        acknowledge(toastId)
        if (action)
            action()
    }

    function info(message, timeout) { return post("info", message, timeout) }
    function success(message, timeout) { return post("success", message, timeout) }
    function warning(message, timeout) { return post("warning", message, timeout) }
    function error(message, timeout) { return post("error", message, timeout) }

    // The toast in front: the newest one not on its way out, or -1.
    function frontId() {
        for (let i = model.count - 1; i >= 0; --i) {
            if (!model.get(i).exiting)
                return model.get(i).toastId
        }
        return -1
    }

    // Toasts waiting behind the front one.
    function queuedBehind() {
        let n = 0
        for (let i = 0; i < model.count; ++i) {
            if (!model.get(i).exiting)
                ++n
        }
        return Math.max(0, n - 1)
    }

    // A dismissal by hand (X, swipe, action): the user saw it, so it is not unread.
    function acknowledge(toastId) {
        _markRead(toastId)
        dismiss(toastId)
    }

    // Dismissal is two-stage: this flags the row, the host animates it out, and
    // the host calls remove() when the animation finishes. Removing the row here
    // would destroy the delegate mid-frame, so a dismissed toast simply vanished.
    function dismiss(toastId) {
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i).toastId === toastId) {
                if (!model.get(i).exiting) {
                    model.setProperty(i, "exiting", true)
                    revision++
                }
                return
            }
        }
    }

    function remove(toastId) {
        if (!_inHistory(toastId))
            delete _actions[toastId]
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i).toastId === toastId) {
                model.remove(i)
                revision++
                return
            }
        }
    }

    function markAllRead() {
        for (let i = 0; i < history.count; ++i)
            history.setProperty(i, "unread", false)
        unreadCount = 0
    }

    function clearHistory() {
        for (let i = 0; i < history.count; ++i) {
            const id = history.get(i).toastId
            if (!_isQueued(id))
                delete _actions[id]
        }
        history.clear()
        unreadCount = 0
    }

    function clear() {
        model.clear()
        _actions = ({})
        history.clear()
        unreadCount = 0
        revision++
    }

    function _markRead(toastId) {
        for (let i = 0; i < history.count; ++i) {
            if (history.get(i).toastId === toastId) {
                if (history.get(i).unread) {
                    history.setProperty(i, "unread", false)
                    unreadCount--
                }
                return
            }
        }
    }

    function _setHistory(toastId, role, value) {
        for (let i = 0; i < history.count; ++i) {
            if (history.get(i).toastId === toastId) {
                history.setProperty(i, role, value)
                return
            }
        }
    }

    function _inHistory(toastId) {
        for (let i = 0; i < history.count; ++i) {
            if (history.get(i).toastId === toastId)
                return true
        }
        return false
    }

    function _isQueued(toastId) {
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i).toastId === toastId)
                return true
        }
        return false
    }
}

pragma Singleton
import QtQuick

// App-wide notification queue.
//
// Before this, user-facing failures had nowhere to go: import errors, save
// errors, `subtitleGenerationFinished` and `exportFinished` were never surfaced,
// and `lastMessage` rendered as static muted text in the header with no severity
// and no dismiss. Call Toasts.error(...) / .success(...) from anywhere; the
// visual host lives once in Main.qml.
QtObject {
    id: toasts

    readonly property ListModel model: ListModel { }

    // Bumped per post so each entry has a stable identity for removal.
    property int _nextId: 1

    // Errors stay until dismissed — a failure the user missed is worse than a
    // lingering banner. Everything else auto-expires.
    readonly property int defaultTimeout: 5000
    readonly property int maxVisible: 4

    function post(severity, message, timeout) {
        if (!message || message.length === 0)
            return -1

        // Collapse an identical consecutive message into a repeat count instead
        // of stacking duplicates (transform-blocked fires repeatedly on drag).
        if (model.count > 0) {
            const last = model.get(model.count - 1)
            // Never fold a repeat into a toast that is already animating out.
            if (last.message === message && last.severity === severity && !last.exiting) {
                model.setProperty(model.count - 1, "repeats", last.repeats + 1)
                return last.toastId
            }
        }

        const id = _nextId++
        model.append({
            toastId: id,
            severity: severity,
            message: message,
            repeats: 1,
            exiting: false,
            timeout: timeout !== undefined ? timeout
                                           : (severity === "error" ? 0 : defaultTimeout)
        })

        while (model.count > maxVisible)
            model.remove(0)

        return id
    }

    function info(message, timeout) { return post("info", message, timeout) }
    function success(message, timeout) { return post("success", message, timeout) }
    function warning(message, timeout) { return post("warning", message, timeout) }
    function error(message, timeout) { return post("error", message, timeout) }

    // Dismissal is two-stage: this flags the row, the host animates it out, and
    // the host calls remove() when the animation finishes. Removing the row here
    // would destroy the delegate mid-frame, so a dismissed toast simply vanished.
    // A Column positioner has add/move/populate but no `remove` transition, so
    // there is no declarative way to do this from the host alone.
    function dismiss(toastId) {
        for (var i = 0; i < model.count; ++i) {
            if (model.get(i).toastId === toastId) {
                if (!model.get(i).exiting)
                    model.setProperty(i, "exiting", true)
                return
            }
        }
    }

    function remove(toastId) {
        for (var i = 0; i < model.count; ++i) {
            if (model.get(i).toastId === toastId) {
                model.remove(i)
                return
            }
        }
    }

    function clear() {
        model.clear()
    }
}

#pragma once

#include <QElapsedTimer>
#include <QObject>

// Touch feedback for the timeline, in the vocabulary of the interaction rather than of the
// actuator: call sites say what just happened and this decides what it feels like. The mapping onto
// platform constants lives in android/src/org/cutwire/drift/Haptics.java.
//
// Two things every call site gets for free, and which are the difference between this feeling
// deliberate and feeling broken:
//
//   * A rate limit. A drag across a dense timeline crosses snap targets faster than a motor can
//     resolve them, and one tick per move event is a continuous rumble, not feedback.
//   * Edge triggering, for the two states that persist across many move events — the snap a clip is
//     currently held by, and the track it is currently over. Those report their *identity* on every
//     update and only fire when it changes, so parking on a snap stays silent and moving between
//     two snaps a frame apart still ticks twice.
//
// Every entry point is a no-op off Android, so QML calls into it unconditionally; the desktop
// timeline shares these components.
namespace drift {

class Haptics : public QObject
{
    Q_OBJECT
    // Whether the device can do this at all — no actuator, or not Android. The settings row is
    // hidden when false, because a switch over a motor that does not exist explains nothing.
    Q_PROPERTY(bool supported READ isSupported CONSTANT FINAL)
    // The app's own opt-out, on top of the system-wide one the platform already applies. CapCut
    // does not have this and gets asked for it; it costs one persisted bool.
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged FINAL)

public:
    explicit Haptics(QObject *parent = nullptr);

    bool isSupported() const;
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // --- One-shots ----------------------------------------------------------------------------

    // A tap landed on something that is now selected. Only for a selection the user's finger
    // caused: firing this from a programmatic change turns "select all" into twenty ticks.
    Q_INVOKABLE void select();
    // A press-and-hold has been recognised and the thing under the finger is now draggable. The
    // one moment nothing on screen can announce ahead of the user's own movement.
    Q_INVOKABLE void pickUp();
    // A drag ended and the thing came to rest. Clears the latches too — the gesture that was
    // holding them is over.
    Q_INVOKABLE void drop();
    // The finger kept going and the thing did not: a source with no frames left, a minimum
    // duration, a neighbouring clip, the end of a zoom range. Latch this at the call site — it
    // describes a state you can sit in, and the limiter alone would let it buzz at 25 Hz.
    Q_INVOKABLE void boundary();
    // Passed a notch: a slider's default value, a discrete step, a lane.
    Q_INVOKABLE void detent();
    // Something happened that the user cannot undo with the same gesture that caused it — a split,
    // a delete.
    Q_INVOKABLE void confirm();

    // --- Edge-triggered ------------------------------------------------------------------------

    // The timeline position a drag is currently snapped to, or a negative value for "not snapped".
    // Safe to call on every move event; it ticks only when the target changes.
    Q_INVOKABLE void snap(qreal targetSeconds);
    // The track a dragged clip would currently land on, or -1 for none. Same contract as snap().
    Q_INVOKABLE void lane(int trackIndex);
    // One step of a trim drag, taking an AppController::TrimOutcome. Both feelings a trim can
    // produce come through here — the tick as an edge takes a snap, and the refusal as it meets a
    // limit — because they are the same gesture and each has to know what the other just did.
    Q_INVOKABLE void trimStep(int outcome);

    // --- Gesture boundaries --------------------------------------------------------------------

    // Forget every latch. Call at the start of a gesture, and at the end of one that does not
    // finish with drop(); otherwise a drag that ended snapped leaves the next one silent.
    Q_INVOKABLE void reset();

signals:
    void enabledChanged();

private:
    // Kept in step with the intent constants in Haptics.java.
    enum Effect {
        Select = 0,
        Snap = 1,
        PickUp = 2,
        Drop = 3,
        Boundary = 4,
        Detent = 5,
        Confirm = 6,
    };

    void fire(Effect effect);

    bool m_enabled = true;
    // Started at construction, never restarted: it is only ever read as a monotonic clock.
    QElapsedTimer m_clock;
    qint64 m_lastFiredMs = 0;
    // Mirrors AppController::TrimOutcome. Compared, never fired — the enum crosses this boundary
    // as a plain int so the feedback layer does not have to know about the editing model.
    enum TrimState {
        TrimNone = 0,
        TrimMoved = 1,
        TrimSnapped = 2,
        TrimBlocked = 3,
    };

    // Latch state for the edge-triggered entry points.
    qreal m_snapTarget = -1;
    int m_lane = -1;
    int m_trimState = TrimNone;
};

} // namespace drift

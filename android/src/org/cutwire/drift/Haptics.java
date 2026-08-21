package org.cutwire.drift;

import android.app.Activity;
import android.os.Build;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.Window;

// Touch feedback for the timeline. Everything goes through View.performHapticFeedback rather than
// Vibrator: the view route needs no VIBRATE permission and, more importantly, the platform gates it
// on the user's own "vibration feedback" setting for free. Vibrator.vibrate() would fire whether or
// not they had asked for it, which is the complaint CapCut collects for exactly this feature.
//
// Every method here must be called on the Android UI thread — performHapticFeedback goes through
// the view hierarchy, which is not thread-safe, and Qt's GUI thread is a different thread. The C++
// side (drift::Haptics) hops before calling, the same way AudioFocus does.
public class Haptics
{
    // Intents, not effects: the call site says what happened and the mapping below decides what
    // that feels like on the device it is running on. Kept in step with drift::Haptics::Effect.
    public static final int SELECT = 0;
    public static final int SNAP = 1;
    public static final int PICK_UP = 2;
    public static final int DROP = 3;
    public static final int BOUNDARY = 4;
    public static final int DETENT = 5;
    public static final int CONFIRM = 6;

    // API 34 added constants that name these interactions outright, and a device with a modern
    // actuator renders them distinctly. Below that they collapse onto the handful that have been
    // around since API 21 — a tick is still the right shape, it just stops being a *specific* tick.
    // Nothing here ever passes a constant the running platform does not know: an unrecognised value
    // is not a no-op, it falls through to whatever the OEM mapped that integer to.
    private static int constantFor(int intent)
    {
        final int sdk = Build.VERSION.SDK_INT;
        switch (intent) {
        case SELECT:
            return HapticFeedbackConstants.CLOCK_TICK;
        case SNAP:
        case DETENT:
            return sdk >= 34 ? HapticFeedbackConstants.SEGMENT_TICK
                             : HapticFeedbackConstants.CLOCK_TICK;
        case PICK_UP:
            return sdk >= 34 ? HapticFeedbackConstants.DRAG_START
                             : HapticFeedbackConstants.LONG_PRESS;
        case DROP:
            return sdk >= 30 ? HapticFeedbackConstants.GESTURE_END
                             : HapticFeedbackConstants.CLOCK_TICK;
        case BOUNDARY:
            // REJECT is the "that did not happen" effect — the right answer for an edge that
            // refuses to move, and distinct enough from SNAP that the two do not blur together
            // during a trim, which is the one drag that produces both.
            return sdk >= 30 ? HapticFeedbackConstants.REJECT
                             : HapticFeedbackConstants.CLOCK_TICK;
        case CONFIRM:
            return sdk >= 30 ? HapticFeedbackConstants.CONFIRM
                             : HapticFeedbackConstants.VIRTUAL_KEY;
        default:
            return HapticFeedbackConstants.CLOCK_TICK;
        }
    }

    public static void perform(Activity activity, int intent)
    {
        if (activity == null)
            return;
        Window window = activity.getWindow();
        if (window == null)
            return;
        // Fetched per call rather than cached in a static: the decor view is replaced when the
        // activity's content is, and a stale reference is a view with no attach info, which
        // swallows the feedback silently. Two JNI lookups against a rate limiter that lets at most
        // ~25 of these through a second is not worth a staleness bug.
        View view = window.getDecorView();
        if (view == null)
            return;
        // The one-argument overload honours both View.isHapticFeedbackEnabled and the system-wide
        // touch feedback setting. That is the whole point of using this API; do not reach for the
        // FLAG_IGNORE_* overload to "make it reliable".
        view.performHapticFeedback(constantFor(intent));
    }

    // True when the device has an actuator at all. Tablets and emulators frequently do not, and a
    // settings switch that controls nothing is worse than no switch.
    public static boolean hasVibrator(Activity activity)
    {
        if (activity == null)
            return false;
        android.os.Vibrator vibrator = activity.getSystemService(android.os.Vibrator.class);
        return vibrator != null && vibrator.hasVibrator();
    }
}

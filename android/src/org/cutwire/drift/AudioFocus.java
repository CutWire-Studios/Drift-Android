package org.cutwire.drift;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;

// Preview audio is media playback like any other app's, and Android arbitrates that with audio
// focus. Without a request the preview mixes on top of whatever the user is already listening to
// and nothing ever tells it to stop; without the becoming-noisy broadcast it keeps playing out of
// the loudspeaker the instant a headphone is unplugged, which is the failure that happens in
// public. Both land in the same place: pause the engine, which is all the native callback does.
//
// Every method here runs on the Android UI thread — the C++ side hops there before calling — and
// that is the thread the focus listener and the receiver are dispatched on too, so the static
// state below needs no locking.
public class AudioFocus
{
    private static AudioFocusRequest sRequest;
    private static BroadcastReceiver sNoisyReceiver;

    // The preview has nothing useful to say at a lower volume, so a duckable loss is treated like
    // any other: pause, and let the user press play again when their call or navigation prompt is
    // over. Resuming automatically would start audio the user is no longer looking at.
    private static final AudioManager.OnAudioFocusChangeListener sListener = focusChange -> {
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS
            || focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT
            || focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) {
            nativePausePlayback();
        }
    };

    public static void request(Context context)
    {
        AudioManager manager = context.getSystemService(AudioManager.class);
        if (manager == null)
            return;

        if (sRequest == null) {
            AudioAttributes attributes = new AudioAttributes.Builder()
                                             .setUsage(AudioAttributes.USAGE_MEDIA)
                                             .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
                                             .build();
            // GAIN_TRANSIENT, not GAIN: editing is a session of short plays between edits, and a
            // permanent gain would tell a music player it has been stopped for good every time.
            sRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                           .setAudioAttributes(attributes)
                           .setOnAudioFocusChangeListener(sListener)
                           .build();
        }
        manager.requestAudioFocus(sRequest);

        if (sNoisyReceiver == null) {
            sNoisyReceiver = new BroadcastReceiver() {
                @Override
                public void onReceive(Context received, Intent intent)
                {
                    nativePausePlayback();
                }
            };
            // Registered against the application context, and unregistered against the same one in
            // abandon(): a receiver left behind would outlive every play/pause cycle and pile up.
            context.getApplicationContext().registerReceiver(
                sNoisyReceiver, new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY));
        }
    }

    public static void abandon(Context context)
    {
        AudioManager manager = context.getSystemService(AudioManager.class);
        // sRequest is kept: it is the same object abandonAudioFocusRequest matches on, and the next
        // play() reuses it.
        if (manager != null && sRequest != null)
            manager.abandonAudioFocusRequest(sRequest);

        if (sNoisyReceiver != null) {
            context.getApplicationContext().unregisterReceiver(sNoisyReceiver);
            sNoisyReceiver = null;
        }
    }

    // Registered from C++ (PlaybackEngine) rather than resolved by name, so the symbol is free to
    // stay internal to the playback code.
    private static native void nativePausePlayback();
}

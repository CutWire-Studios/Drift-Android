package org.cutwire.drift;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

// An export renders for minutes inside the app process. Backgrounded, that process is frozen and
// then killed, and the render is gone with nothing to resume from. A foreground service is what
// keeps it scheduled; the progress notification is not decoration but the price Android charges
// for one. The encode itself stays in the app process — this service only holds it there, so it
// starts and stops with Exporter::run and never restarts on its own.
public class ExportService extends Service
{
    private static final String CHANNEL_ID = "export";
    private static final int NOTIFICATION_ID = 4711;

    // The service now holds any long job, not just an export, so the notification says which one.
    // volatile because start() runs on the job's thread while onStartCommand and setPercent read it
    // from the main thread.
    private static volatile String sTitle = "Exporting";

    public static void start(Context context, String title)
    {
        if (title != null && !title.isEmpty())
            sTitle = title;
        start(context);
    }

    // Called from the export thread, hence the hop to the UI thread for the permission prompt;
    // the rest of the Context API here is thread-safe.
    public static void start(Context context)
    {
        if (context instanceof Activity) {
            Activity activity = (Activity) context;
            activity.runOnUiThread(() -> requestNotificationPermission(activity));
        }
        createChannel(context);
        context.startForegroundService(new Intent(context, ExportService.class));
    }

    // False once the service is gone — a refused startForeground, the FGS timeout, or a normal stop.
    // The job it was protecting keeps running and keeps reporting, and notify() does not need a live
    // service, so without this every later percent would re-post an ongoing notification that
    // nothing is left to take down.
    private static volatile boolean sForeground = false;

    // Updates the same notification the service is holding, rather than restarting the service
    // once per percent.
    public static void setPercent(Context context, int percent)
    {
        if (!sForeground)
            return;
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager != null)
            manager.notify(NOTIFICATION_ID, buildNotification(context, percent));
    }

    public static void stop(Context context)
    {
        sForeground = false;
        context.stopService(new Intent(context, ExportService.class));
        // stopService takes the notification down with the service, but not one posted by setPercent
        // after a start that was refused.
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager != null)
            manager.cancel(NOTIFICATION_ID);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId)
    {
        Notification notification = buildNotification(this, 0);
        // API 34+ can refuse the start outright — the mediaProcessing budget is exhausted, or the
        // app was not in a state allowed to promote a service — and it refuses by throwing on the
        // main thread, which is a crash rather than a failed export. Exporter::run does not depend
        // on the service; degrade to an unprotected render, which is what a pre-34 device does
        // anyway, instead of taking the process down mid-encode.
        try {
            if (Build.VERSION.SDK_INT >= 34) {
                startForeground(NOTIFICATION_ID, notification,
                                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING);
            } else {
                startForeground(NOTIFICATION_ID, notification);
            }
            sForeground = true;
        } catch (RuntimeException e) {
            giveUp();
            return START_NOT_STICKY;
        }
        return START_NOT_STICKY;
    }

    // The notification has to go explicitly: stopSelf leaves behind one posted by a startForeground
    // that never took, and an ongoing notification for a job with no service is unswipeable.
    private void giveUp()
    {
        sForeground = false;
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null)
            manager.cancel(NOTIFICATION_ID);
        stopSelf();
    }

    // A mediaProcessing service gets a cumulative 6 h per 24 h from API 35 on. Past that the
    // platform calls this and kills the app if the service is still up a few seconds later, so
    // stopping is the only correct answer — the export itself keeps running unprotected, exactly
    // as it does when startForeground is refused above. API 35 dispatches the one-argument form
    // and API 36 the typed one; both are overridden because neither device calls the other's.
    @Override
    public void onTimeout(int startId)
    {
        giveUp();
    }

    @Override
    public void onTimeout(int startId, int fgsType)
    {
        giveUp();
    }

    @Override
    public IBinder onBind(Intent intent)
    {
        return null;
    }

    private static void createChannel(Context context)
    {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager == null)
            return;
        NotificationChannel channel =
            new NotificationChannel(CHANNEL_ID, "Export", NotificationManager.IMPORTANCE_LOW);
        channel.setShowBadge(false);
        manager.createNotificationChannel(channel);
    }

    private static Notification buildNotification(Context context, int percent)
    {
        Intent launch = context.getPackageManager()
                            .getLaunchIntentForPackage(context.getPackageName());
        PendingIntent contentIntent = launch == null
            ? null
            : PendingIntent.getActivity(context, 0, launch,
                                        PendingIntent.FLAG_IMMUTABLE
                                            | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(context, CHANNEL_ID)
                   .setContentTitle(sTitle)
                   .setContentText(percent + "%")
                   .setSmallIcon(android.R.drawable.stat_sys_upload)
                   .setProgress(100, percent, false)
                   .setOngoing(true)
                   .setContentIntent(contentIntent)
                   .build();
    }

    // Without the permission the service still runs and still protects the export; only the
    // notification is withheld, which is also what makes the export look like it stalled.
    private static void requestNotificationPermission(Activity activity)
    {
        if (Build.VERSION.SDK_INT < 33)
            return;
        if (activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
            == PackageManager.PERMISSION_GRANTED)
            return;
        // Qt hands every onRequestPermissionsResult to its own pending-request table; a request
        // code well clear of the ones it hands out keeps this prompt from being mistaken for one
        // of them.
        activity.requestPermissions(new String[] { Manifest.permission.POST_NOTIFICATIONS }, 0x51E1);
    }
}

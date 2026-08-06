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

    // Updates the same notification the service is holding, rather than restarting the service
    // once per percent.
    public static void setPercent(Context context, int percent)
    {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager != null)
            manager.notify(NOTIFICATION_ID, buildNotification(context, percent));
    }

    public static void stop(Context context)
    {
        context.stopService(new Intent(context, ExportService.class));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId)
    {
        Notification notification = buildNotification(this, 0);
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, notification,
                            ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
        return START_NOT_STICKY;
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
                   .setContentTitle("Exporting")
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

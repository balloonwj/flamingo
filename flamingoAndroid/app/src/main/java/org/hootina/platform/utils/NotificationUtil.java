package org.hootina.platform.utils;

import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.support.v7.app.NotificationCompat;

public class NotificationUtil {

    public static void showNotification(Context context, int id, String title, String text, int drawableid, Class penttingClass) {
        NotificationCompat.Builder builder = new NotificationCompat.Builder(context);
        builder.setSmallIcon(drawableid);
        builder.setContentTitle(title);
        builder.setContentText(text);
        builder.setAutoCancel(true);
        builder.setOnlyAlertOnce(true);

        //自定义打开的界面
        Intent resultIntent = new Intent(context, penttingClass);
        resultIntent.setFlags(Intent.FLAG_ACTIVITY_FORWARD_RESULT);
        PendingIntent contentIntent = PendingIntent.getActivity(context, 0,
                resultIntent, PendingIntent.FLAG_UPDATE_CURRENT);
        builder.setContentIntent(contentIntent);

        NotificationManager notificationManager = (NotificationManager) context
                .getSystemService(Context.NOTIFICATION_SERVICE);
        notificationManager.notify(id, builder.build());
    }

}

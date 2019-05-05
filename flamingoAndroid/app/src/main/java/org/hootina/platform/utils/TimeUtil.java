package org.hootina.platform.utils;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;

/**
 * @author: zhangyl
 * @date:
 */
public class TimeUtil {

    public static boolean isToday(Date date) {
        if (date == null)
            return false;

        Date now = new Date();
        int year = now.getYear()  + 1900;
        int month = now.getMonth() + 1;
        int day = now.getDate();

        String todayMidnightStr = String.format("%04d-%02d-%02d 00:00:00", year, month, day);
        SimpleDateFormat format = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");

        Date todayMidnight = null;
        try {
            todayMidnight = format.parse(todayMidnightStr);
        } catch (ParseException e) {
            //e.printStackTrace();
            return false;
        }

        if (date.getTime() - todayMidnight.getTime() > 0L && date.getTime() - todayMidnight.getTime() < 24 * 60 * 60 * 1000L)
            return true;

        return false;
    }


    public static boolean isYesterday(Date date) {
        if (date == null)
            return false;

        Date now = new Date();
        int year = now.getYear() + 1900;
        int month = now.getMonth() + 1;
        int day = now.getDate();

        String todayMidnightStr = String.format("%04d-%02d-%02d 00:00:00", year, month, day);
        SimpleDateFormat format = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");

        Date todayMidnight = null;
        try {
            todayMidnight = format.parse(todayMidnightStr);
        } catch (ParseException e) {
            //e.printStackTrace();
            return false;
        }

        if (todayMidnight.getTime() - date.getTime() > 0L && todayMidnight.getTime() - date.getTime() < 24 * 60 * 60 * 1000L)
            return true;

        return false;
    }

    //是否是今年
    public static boolean isCurrentYear(Date date) {
        if (date == null)
            return false;

        Date now = new Date();
        int year = now.getYear() + 1900;
        if (year == date.getYear() + 1900)
            return true;

        return false;
    }

    public static String getFormattedTimeString(Date d) {
        String str = "";

        if (isToday(d)) {
            str = String.format("%02d:%02d", d.getHours(), d.getMinutes());
        } else if (isYesterday(d)) {
            str = String.format("昨天 %02d:%02d", d.getHours(), d.getMinutes());
        } else if (isCurrentYear(d)) {
            str = String.format("%2d-%2d %02d:%02d", d.getMonth() + 1, d.getDate(), d.getHours(), d.getMinutes());
        } else {
            str = String.format("%04d-%2d-%2d %02d:%02d", d.getYear() + 1900, d.getMonth() + 1, d.getDate(), d.getHours(), d.getMinutes());
        }

        return str;
    }
}

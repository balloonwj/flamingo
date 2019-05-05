package org.hootina.platform.utils;

import android.os.Environment;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.FileOutputStream;

import java.text.SimpleDateFormat;
import java.util.Date;

/**
 * 往sd卡写入日志文件
 * Created by zhangyl on 2018/1/6.
 */

public final class LoggerFile {
    private final static int LOG_INFO = 0;
    private final static int LOG_WARNING = 1;
    private final static int LOG_ERROR = 2;
    private static File             mFile;
    private static FileOutputStream mOutputStream;

    /**
     * @param toFile 为true写入文件，为false写入控制台
     * @return
     */
    public static boolean Init(boolean toFile)
    {
        Date curDate = new Date(System.currentTimeMillis());
        SimpleDateFormat formatter = new SimpleDateFormat("yyyyMMddHHmmssSSS");
        String timestr = formatter.format(curDate);
        String logFilename = String.format("flamingo/Logs/flamingo%s.log", timestr);

        mFile = new File(Environment.getExternalStorageDirectory(), logFilename);
        try {
            mFile.createNewFile();
        } catch (IOException e) {
            e.printStackTrace();
            return false;
        }

        try {
            mOutputStream = new FileOutputStream(mFile);
        } catch (FileNotFoundException e) {
            e.printStackTrace();
            return false;
        }

        return true;
    }

    public static boolean Uninit()
    {
        if (mOutputStream != null) {
            try {
                mOutputStream.close();
            }catch (IOException e){
                e.printStackTrace();
                return false;
            }
        }

        return true;
    }

    public static boolean LogInfo(String... str)
    {
        return Log(LOG_INFO, str);
    }

    public static boolean LogWarning(String... str)
    {
        return Log(LOG_WARNING, str);
    }

    public static boolean LogError(String... str)
    {
        return Log(LOG_ERROR, str);
    }

    private static boolean Log(int level, String... str)
    {
        if (mOutputStream == null || str.length == 0)
            return false;

        String levelStr;
        if (level == LOG_INFO)
            levelStr = "[INFO]";
        else if (level == LOG_WARNING)
            levelStr = "[WARNING]";
        else if (level == LOG_ERROR)
            levelStr = "[ERROR]";
        else
            levelStr = "[ERROR]";

        //前面加上日期和级别信息
        Date curDate = new Date(System.currentTimeMillis());
        SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss:SSS");
        String timeStr = formatter.format(curDate);
        String prefix = String.format("[%s]%s", timeStr, levelStr);

        StackTraceElement[] trace = new Throwable().getStackTrace();
        // 下标为0的元素是上一行语句的信息, 下标为1的才是调用printLine的地方的信息
        StackTraceElement tmp = trace[1];

        //String lineNoStr = String.format("[%s:%d]", tmp.getFileName(), tmp.getLineNumber());
        String threadIDStr = String.format("[ThreadID %d]", Thread.currentThread().getId());
        StringBuilder builder = new StringBuilder();
        //builder.append(lineNoStr);
        builder.append(prefix);
        builder.append(threadIDStr);
        for (int i = 0; i < str.length; ++i)
        {
            builder.append(str[i]);
        }
        builder.append("\n");
        String actualLogToWrite = builder.toString();

        byte[] bt = actualLogToWrite.getBytes();
        if (bt == null)
            return false;

        try {
            mOutputStream.write(bt, 0, bt.length);
            mOutputStream.flush();
        } catch (IOException e) {
            e.printStackTrace();
            return false;
        }

        return true;
    }

}

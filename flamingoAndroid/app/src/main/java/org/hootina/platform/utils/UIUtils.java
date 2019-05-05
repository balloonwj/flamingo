package org.hootina.platform.utils;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Environment;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.TextUtils;
import android.text.style.ImageSpan;
import android.util.Log;
import android.view.View;
import android.view.inputmethod.InputMethodManager;

import org.hootina.platform.R;
import org.hootina.platform.activities.SplashActivity;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * UI工具类
 */

public class UIUtils {
    //隐藏软键盘
    public static void hideSoftInput(Context context, View v)
    {
        InputMethodManager imm = (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
        imm.hideSoftInputFromWindow(v.getWindowToken(), 0);
    }

    public static class FileUtils {
        public static String SDPATH = Environment.getExternalStorageDirectory()
                + "/formats/";
        /**
         * @param context
         * @return
         */
        public static List<String> getEmojiFile(Context context) {
            try {
                List<String> list = new ArrayList<String>();
                InputStream in = context.getResources().getAssets().open("emoji");//
                BufferedReader br = new BufferedReader(new InputStreamReader(in,"UTF-8"));
                String str = null;
                while ((str = br.readLine()) != null) {
                    list.add(str);
                }

                return list;
            } catch (IOException e) {
                e.printStackTrace();
            }
            return null;
        }
        public static void delFile(String fileName)
        {
            File file = new File(SDPATH + fileName);
            if (file.isFile())
            {
                file.delete();
            }
            file.exists();
        }
    }


}

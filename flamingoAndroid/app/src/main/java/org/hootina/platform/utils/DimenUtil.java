package org.hootina.platform.utils;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.DisplayMetrics;
import android.view.WindowManager;


import org.hootina.platform.FlamingoApplication;

/**
 * Created by RWX on 2017/11/24.
 */
public class DimenUtil {
    public static float getScale() {
        return FlamingoApplication.getInstance().getResources().getDisplayMetrics().density;
    }

    public static int dp2px(float dipValue) {
        final float scale = FlamingoApplication.getInstance().getResources().getDisplayMetrics().density;
        return (int) (dipValue * scale + 0.5f);
    }


    public static int getScreenWidth() {
        DisplayMetrics displayMetrics = FlamingoApplication.getInstance().getResources().getDisplayMetrics();
        return displayMetrics.widthPixels;
    }

    public static int getScreenHeight() {
        Context context = FlamingoApplication.getInstance();
        WindowManager wm = (WindowManager) context
                .getSystemService(Context.WINDOW_SERVICE);

//int width = wm.getDefaultDisplay().getWidth();
        int height = wm.getDefaultDisplay().getHeight();

        return height;

    }

    public static int getWindowWidth() {
        return getWindowManager(FlamingoApplication.getInstance()).getDefaultDisplay().getWidth();
    }

    public static WindowManager getWindowManager(Context paramContext) {
        return (WindowManager) paramContext.getSystemService(Context.WINDOW_SERVICE);
    }

    public static int getStatusBarHeight() {
        Context context = FlamingoApplication.getInstance();
        int statusBarHeight = -1;
        //获取status_bar_height资源的ID
        int resourceId = context.getResources().getIdentifier("status_bar_height", "dimen", "android");
        if (resourceId > 0) {
            //根据资源ID获取响应的尺寸值
            statusBarHeight = context.getResources().getDimensionPixelSize(resourceId);
        }

        return statusBarHeight;
    }

    public static int getNavigationBarHeight() {
        Context context = FlamingoApplication.getInstance();
        int resourceId = context.getResources().getIdentifier("navigation_bar_height", "dimen", "android");
        int height = context.getResources().getDimensionPixelSize(resourceId);
        return height;
    }

    public static int getActionBarHeight() {
        Context context = FlamingoApplication.getInstance();
        TypedArray actionbarSizeTypedArray = context.obtainStyledAttributes(new int[]{
                android.R.attr.actionBarSize
        });

        return (int) actionbarSizeTypedArray.getDimension(0, 0);

    }


}
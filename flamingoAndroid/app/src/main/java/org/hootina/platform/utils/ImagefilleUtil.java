package org.hootina.platform.utils;

import android.graphics.BitmapFactory;
import android.graphics.BitmapFactory.Options;

public class ImagefilleUtil {
	 public static boolean isImageFile(String filePath) {
	        Options options = new Options();
	        options.inJustDecodeBounds = true;
	        BitmapFactory.decodeFile(filePath, options);
	        if (options.outWidth == -1) {
	            return false;
	        }
	        return true;
	 }    
}

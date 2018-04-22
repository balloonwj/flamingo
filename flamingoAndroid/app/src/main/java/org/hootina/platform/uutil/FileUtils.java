package org.hootina.platform.uutil;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

import android.content.Context;

import java.io.File;

import android.os.Environment;

public class FileUtils {
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

package org.hootina.platform.utils;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

import android.content.Context;
import android.content.res.AssetManager;
import android.database.sqlite.SQLiteDatabase;

public class SQLdm {

	private final static String dbName = "china_province_city_zone.db";
	private SQLiteDatabase database;

	public static SQLiteDatabase openDatabase(Context context) {

		String pathStr = "data/data/" + context.getPackageName();
		String filePath = pathStr + "/" + dbName;

		File jhPath = new File(filePath);
		// 查看数据库文件是否存在
		if (jhPath.exists()) {
			// 存在则直接返回打开的数据库
			return SQLiteDatabase.openOrCreateDatabase(jhPath, null);
		} else {
			// 不存在先创建文件夹
			File path = new File(pathStr);
			try {
				// 得到资源
				AssetManager am = context.getAssets();
				// 得到数据库的输入流
				InputStream is = am.open(dbName);
				// 用输出流写到SDcard上面
				FileOutputStream fos = new FileOutputStream(jhPath);
				// 创建byte数组 用于1KB写一次
				byte[] buffer = new byte[1024];
				int count = 0;
				while ((count = is.read(buffer)) > 0) {
					fos.write(buffer, 0, count);
				}
				// 最后关闭就可以了
				fos.flush();
				fos.close();
				is.close();
			} catch (IOException e) {
				e.printStackTrace();
				return null;
			}
			// 如果没有这个数据库 我们已经把他写到SD卡上了，然后在执行一次这个方法 就可以返回数据库了
			return openDatabase(context);
		}
	}
}

package org.hootina.platform.utils;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.support.v4.util.LruCache;
import android.util.Log;

import org.hootina.platform.result.FileInfo;
import org.hootina.platform.userinfo.UserInfo;

import com.lidroid.xutils.BitmapUtils;

public class PictureUtil {
	private static List<FileInfo> m_downloadingFiles;
	private static List<String> mFailFile = new ArrayList<String>();
	
	private static Map<Integer, String> m_mapHeadPath = new HashMap<Integer, String>();
	
	private static BitmapUtils bitmapUtils;
	
	//private DiskLruCache mDiskLruCache;
	private static LruCache<String, Bitmap> mMemoryCache;

	public static BitmapUtils GetBitmapUtil()
	{
		return bitmapUtils;
	}
	
	public static void CreateBitmapUtils(Context cont)
	{
		bitmapUtils = new BitmapUtils(cont, "/sdcard/org.org.hootina/", 0.4f, 1024 * 1024 * 200);
	}
	
	public static void Init()
	{
		// 获取虚拟机可用内存（内存占用超过该值的时候，将报OOM异常导致程序崩溃）。最后除以1024是为了以kb为单位
	    final int maxMemory = (int) (Runtime.getRuntime().maxMemory() / 1024);
	    
	 // 使用可用内存的1/8来作为Memory Cache
	    final int cacheSize = maxMemory / 8;
	 
	    mMemoryCache = new LruCache<String, Bitmap>(cacheSize) {
	        @Override
	        protected int sizeOf(String key, Bitmap bitmap) {
	            // 重写sizeOf()方法，使用Bitmap占用内存的kb数作为LruCache的size
	            return getBitmapSize(bitmap) / 1024;
	        }
	    };
	}
	
	public static int getBitmapSize(Bitmap bitmap){
	    return bitmap.getRowBytes() * bitmap.getHeight();                //earlier version
	}
	
	public static void addBitmapToMemoryCache(String key, Bitmap bitmap) {
	    if (getBitmapFromMemCache(key) == null) {
	        mMemoryCache.put(key, bitmap);
	    }
	}
	 
	public static Bitmap getBitmapFromMemCache(String key) {
	    return mMemoryCache.get(key);
	}
	
	public static void deleteFromMemCache(String key) {
		mMemoryCache.remove(key);
	}
	
	// 下载图片请求
	public static void loadfile(String strLocalName, String strPic) {
		// 对于本次已经下载失败的不再重新下载
		for(int i = 0; i < mFailFile.size(); ++i)
		{
			if(strLocalName.equals(mFailFile.get(i)))
			{
				return;
			}
		}

		String path = "/sdcard/org.org.hootina/" + strPic;
		File file = new File(path);
		if (file.exists()) {

		} else {
			FileInfo info = new FileInfo();
			info.localFileName = strLocalName;
			info.setName(strPic);
			// contwo.Push(info);

			int nFound = 0;
			if (m_downloadingFiles == null) {
				m_downloadingFiles = new ArrayList<FileInfo>();
			}

			for (int i = 0; i < m_downloadingFiles.size(); ++i) {
				if (m_downloadingFiles.get(i).getName()
						.equalsIgnoreCase(info.getName())) {
					nFound = 1;
					break;
				}
			}

			if (nFound == 0) {
				m_downloadingFiles.add(info);

//				if (m_downloadingFiles.size() == 1) {
//					contwo.sendFileLoadInfo(info.getName().getBytes(), 0,
//							500000, 0, 0, 0);
//				}
			}
		}
	}
	
	public static void loadFailed()
	{
		if(m_downloadingFiles == null)
		{
			return;
		}
		mFailFile.add(m_downloadingFiles.get(0).localFileName);
		
		m_downloadingFiles.remove(0);
		
//		if(m_downloadingFiles.size() > 0)
//		{
//			FileInfo info = m_downloadingFiles.get(0);
//			contwo.sendFileLoadInfo(info.getName().getBytes(), 0,
//					500000, 0, 0, 0);
//		}
	}
	
	public static void clear()
	{
		if(m_downloadingFiles != null)
		{
			m_downloadingFiles.clear();
		}
	}

	// 下载完成
	public static void loadfiledata(int uDownsize, int uFilesize,
			byte[] filebyte) {
		m_downloadingFiles.get(0).setData(
				ByteUtil.byteMerger(m_downloadingFiles.get(0).getData(),
						filebyte));

		m_downloadingFiles.get(0).setSize(
				m_downloadingFiles.get(0).getSize() + uDownsize);
		if (m_downloadingFiles.get(0).getSize() >= uFilesize) {
			FileOutputStream fop = null;
			// try {

//			if (uFilesize > 200 * 1024) {
//				Bitmap bm = BitmapFactory.decodeByteArray(m_downloadingFiles
//						.get(0).getData(), 0, uFilesize);
//
//				saveMyBitmap(bm, m_downloadingFiles.get(0).localFileName);
//			} else {
				try {

					File file = null;
					if (m_downloadingFiles.get(0).localFileName.contains("/")) {
						
						String path = "/sdcard/org.org.hootina/" + m_downloadingFiles.get(0).localFileName.split("/")[0];
								
						file = new File(path);
						        
						if (!file.exists())
						{
							file.mkdir();
						}
						
						file = new File("/sdcard/org.org.hootina/"
								+ m_downloadingFiles.get(0).localFileName);
					} else {
						file = new File("/sdcard/org.org.hootina/"
								+ m_downloadingFiles.get(0).localFileName);
					}
					fop = new FileOutputStream(file);
					try {
						fop.write(m_downloadingFiles.get(0).getData());
						fop.flush();
						fop.close();

					} catch (IOException e) {

						e.printStackTrace();

					}
				} catch (FileNotFoundException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
			//}

			m_downloadingFiles.remove(0);
			
			//tms.User.FileLoadData.Builder fileLoadData = tms.User.FileLoadData.newBuilder();
			//fileLoadData.setUError(0);
			//fileLoadData.setUFilesize(uFilesize);
			//fileLoadData.setUOffset(0);
			//fileLoadData.setUDownsize(uDownsize);
			//fileLoadData.setUTmmodified(0);
			
			//Message msg = new Message();
			//msg.obj = fileLoadData;
			//msg.what = MegAsnType.FileLoadData;
			//BaseActivity.sendMessage(msg);

//			if (m_downloadingFiles.size() > 0) {
//				contwo.sendFileLoadInfo(m_downloadingFiles.get(0).getName()
//						.getBytes(), 0, 500000, 0, 0, 0);
//			}

		} else {
//			contwo.sendFileLoadInfo(m_downloadingFiles.get(0).getName()
//					.getBytes(), m_downloadingFiles.get(0).getSize(), 500000,
//					0, 0, 0);
		}
	}

	
	public static void topictrue(String name, int uError, int uOffset, int uDownsize) {
		String path = "/sdcard/org.org.hootina/" + name;
		File file = new File(path);
		if (file.exists()) {
			FileInputStream fis = null;
			// byte[] szData = getBytesFromFile(file);
			long uFilesize = file.length();

			if (uError == 0) {
				if (uDownsize + uOffset >= uFilesize) {

					uDownsize = (int) uFilesize - uOffset;
				}

				if (uDownsize <= 0) {
					uError = 8;
				} else {
					byte[] szTemp = new byte[uDownsize];
					BufferedInputStream instream;
					try {
						instream = new BufferedInputStream(new FileInputStream(
								file));
						try {
							instream.skip(uOffset);
							instream.read(szTemp, 0, uDownsize);
							instream.close();
						} catch (IOException ex) {
						}
					} catch (FileNotFoundException ex) {
					}

//					contwo.sendFileLoadData(uError, (int) uFilesize, uOffset,
//							uDownsize, 0, szTemp);
				}
			}

		} else {
			uError = 8;
		}

//		if (uError != 0) {
//			contwo.sendFileLoadData(uError, 0, 0, 0, 0, null);
//		}
	}

	//
	public static void topictrues(String name, int uError, int uOffset, int uDownsize) {
		String names = name.split("h/")[1];
		String path = "/sdcard/org.org.hootina/" + names;
		File file = new File(path);
		if (file.exists()) {
			FileInputStream fis = null;
			long uFilesize = file.length();

			if (uError == 0) {
				if (uDownsize + uOffset >= uFilesize) {

					uDownsize = (int) uFilesize - uOffset;
				}

				if (uDownsize <= 0) {
					uError = 8;
				} else {
					byte[] szTemp = new byte[uDownsize];

					BufferedInputStream instream;
					try {
						instream = new BufferedInputStream(new FileInputStream(
								file));
						try {
							instream.skip(uOffset);
							instream.read(szTemp, 0, uDownsize);
							instream.close();
						} catch (IOException ex) {
						}
					} catch (FileNotFoundException ex) {
					}

//					contwo.sendFileLoadData(uError, (int) uFilesize, uOffset,
//							uDownsize, 0, szTemp);
				}
			}

		} else {
			uError = 8;
		}

//		if (uError != 0) {
//			contwo.sendFileLoadData(uError, 0, 0, 0, 0, null);
//		}
	}

	public static File saveMyBitmap(Bitmap mBitmap, String bitName) {
		File f = new File("/sdcard/org.org.hootina/" + bitName);
		if (f.exists()) {
			f.delete();
		}
		FileOutputStream fOut = null;
		try {
			fOut = new FileOutputStream(f);
		} catch (FileNotFoundException e) {
			e.printStackTrace();
		}

		ByteArrayOutputStream out = new ByteArrayOutputStream();
		mBitmap.compress(Bitmap.CompressFormat.JPEG, 100, out);
		int options = 100;
		while (out.toByteArray().length / 1024 > 200 && options >= 10) {
			options -= 10;
			Log.i("yihu save bitmap " + bitName,
					String.valueOf(out.toByteArray().length));
			out.reset();
			mBitmap.compress(Bitmap.CompressFormat.JPEG, options, out);
		}

		try {
			fOut.write(out.toByteArray());
			fOut.flush();
		} catch (IOException e) {
			e.printStackTrace();
		}
		try {
			fOut.close();
		} catch (IOException e) {
			e.printStackTrace();
		}
		return f;
	}
	
	public static void SetHeadPath(int uAccountID, String path)
	{
		if(path != null && !path.equals(""))
		{
			m_mapHeadPath.put(uAccountID, path);
		}		
	}
	
	public static String GetHeadPath(int uAccountID)
	{
		return m_mapHeadPath.get(uAccountID);
	}
	
	public static int orgputeSampleSize(BitmapFactory.Options options,
	        int minSideLength, int maxNumOfPixels) {
	    int initialSize = orgputeInitialSampleSize(options, minSideLength,
	            maxNumOfPixels);
	 
	    int roundedSize;
	    if (initialSize <= 8) {
	        roundedSize = 1;
	        while (roundedSize < initialSize) {
	            roundedSize <<= 1;
	        }
	    } else {
	        roundedSize = (initialSize + 7) / 8 * 8;
	    }
	 
	    return roundedSize;
	}
	 
	private static int orgputeInitialSampleSize(BitmapFactory.Options options,
	        int minSideLength, int maxNumOfPixels) {
	    double w = options.outWidth;
	    double h = options.outHeight;
	 
	    int lowerBound = (maxNumOfPixels == -1) ? 1 :
	            (int) Math.ceil(Math.sqrt(w * h / maxNumOfPixels));
	    int upperBound = (minSideLength == -1) ? 128 :
	            (int) Math.min(Math.floor(w / minSideLength),
	            Math.floor(h / minSideLength));
	 
	    if (upperBound < lowerBound) {
	        return lowerBound;
	    }
	 
	    if ((maxNumOfPixels == -1) &&
	            (minSideLength == -1)) {
	        return 1;
	    } else if (minSideLength == -1) {
	        return lowerBound;
	    } else {
	        return upperBound;
	    }
	} 
	
	public static Bitmap decodePicFromFile(String path, int nServerFlag)
	{
		Bitmap bmTemp = getBitmapFromMemCache("/sdcard/org.org.hootina/" + path);
		if(bmTemp != null)
		{
			return bmTemp;
		}
		File file = new File("/sdcard/org.org.hootina/" + path);
		if (file.exists()) {
			BitmapFactory.Options opts = new BitmapFactory.Options();
			opts.inPreferredConfig = Bitmap.Config.RGB_565;
			opts.inJustDecodeBounds = true;
			BitmapFactory.decodeFile("/sdcard/org.org.hootina/" + path, opts);
			             
			opts.inSampleSize = orgputeSampleSize(opts, -1, 800*1200);
			//这里一定要将其设置回false，因为之前我们将其设置成了true      
			opts.inJustDecodeBounds = false;
			try {
			    Bitmap bmp = BitmapFactory.decodeFile("/sdcard/org.org.hootina/" + path, opts);
			    if(bmp != null)
			    {
			    	addBitmapToMemoryCache("/sdcard/org.org.hootina/" + path, bmp);
			    }
			    return bmp;
			    } catch (OutOfMemoryError err) {
			    }
		}
		else
		{
			if(nServerFlag == 1)
			{
				loadfile(path, path);
			}
		}
		
		return null;
	}
	
	public static Bitmap decodePicFromFullPath(String path)
	{
		Bitmap bmTemp = getBitmapFromMemCache(path);
		if(bmTemp != null)
		{
			return bmTemp;
		}
		File file = new File(path);
		if (file.exists()) {
			BitmapFactory.Options opts = new BitmapFactory.Options();
			opts.inPreferredConfig = Bitmap.Config.RGB_565;
			opts.inJustDecodeBounds = true;
			BitmapFactory.decodeFile(path, opts);
			             
			opts.inSampleSize = orgputeSampleSize(opts, -1, 800*1200);
			//这里一定要将其设置回false，因为之前我们将其设置成了true      
			opts.inJustDecodeBounds = false;
			try {
			    Bitmap bmp = BitmapFactory.decodeFile(path, opts);	
			    if(bmp != null)
			    {
			    	addBitmapToMemoryCache(path, bmp);
			    }
			    return bmp;
			    } catch (OutOfMemoryError err) {
			    }
		}
		return null;
	}
	
	public static Bitmap decodeSystemHead(int nID)
	{		
		return null;
	}
	
	public static Bitmap decodeHeadFromFile(String path)
	{
		Bitmap bmTemp = getBitmapFromMemCache("/sdcard/org.org.hootina/" + path);
		if(bmTemp != null)
		{
			return bmTemp;
		}
		File file = new File("/sdcard/org.org.hootina/" + path);
		if (file.exists()) {
			BitmapFactory.Options opts = new BitmapFactory.Options();
			opts.inPreferredConfig = Bitmap.Config.RGB_565;
			opts.inJustDecodeBounds = true;
			BitmapFactory.decodeFile("/sdcard/org.org.hootina/" + path, opts);
			             
			opts.inSampleSize = orgputeSampleSize(opts, -1, 128*128);
			//这里一定要将其设置回false，因为之前我们将其设置成了true      
			opts.inJustDecodeBounds = false;
			try {
			    Bitmap bmp = BitmapFactory.decodeFile("/sdcard/org.org.hootina/" + path, opts);
			    if(bmp != null)
			    {
			    	addBitmapToMemoryCache("/sdcard/org.org.hootina/" + path, bmp);
			    }
			    return bmp;
			    } catch (OutOfMemoryError err) {
			    }
		}
		else
		{
			loadfile(path, path);
		}
		
		return null;
	}
	
	public static Bitmap getSystemHead(AssetManager mgr, int nFace)
	{
		Bitmap bmp = getBitmapFromMemCache("asserts_" + nFace + ".png");
		if(bmp != null)
			return bmp;
		
		if(nFace >= 0 && mgr != null)
		{
			String strHead = "head" + nFace + ".png";
			try {
				InputStream in=mgr.open(strHead);
				bmp=BitmapFactory.decodeStream(in);
				addBitmapToMemoryCache("asserts_" + nFace + ".png", bmp);
				return bmp;
			}
			catch(Exception e)
			{
				e.printStackTrace();
			}
		}
		return null;
	}
	
	public static Bitmap getHeadPic(AssetManager mgr, UserInfo user)
	{
		if(mgr == null || user == null)
			return null;

		String PicName = user.get_customFacePath();
		Bitmap bmp = null;
		
		if(PicName != null && !PicName.equals(""))
		{
			String path = "/sdcard/flamingo/" + PicName;
			File file = new File(path);

			if (file.exists()) {
				bmp = PictureUtil.decodeHeadFromFile(PicName);

			} else {
				String strLocalName = PicName;
				String strPic = PicName;
				PictureUtil.loadfile(strLocalName, strPic);
			}
		}
		
		if(bmp == null)
		{
			int nFace = user.get_faceType();
			if(nFace == 0 && user.isGroup())
			{
				nFace = 100;
			}
			
			return getSystemHead(mgr, nFace);
		}
		else
		{
			return bmp;
		}
	}
	
	public static Bitmap getFriendHeadPic(AssetManager mgr, UserInfo info)
	{
		if(mgr == null || info == null)
			return null;
		
		String PicName = info.get_customFacePath();
		Bitmap bmp = null;
		
		if(PicName != null && !PicName.equals("")) {
			String path = "/sdcard/flamingo/" + PicName;
			File file = new File(path);

			if (file.exists()) {
				bmp = PictureUtil.decodeHeadFromFile(PicName);

			} else {
				String strLocalName = PicName;
				String strPic = PicName;
				PictureUtil.loadfile(strLocalName, strPic);
			}
		}
		
		if(bmp == null)
		{
			int nFace = info.get_faceType();
			if(nFace == 0 && UserInfo.isGroup(info.get_userid()))
				nFace = 100;

			return getSystemHead(mgr, nFace);
		}

		return bmp;
	}
}

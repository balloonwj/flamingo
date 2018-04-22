package org.hootina.platform.utils;

public class SystemUtils {

	public static final int V2_2 = 8;  
    public static final int V2_3 = 9;  
    public static final int V2_3_3 = 10;  
    public static final int V3_0 = 11;  
    public static final int V3_1 = 12;  
    public static final int V3_2 = 13;  
    public static final int V4_0 = 14;  
    public static final int V4_0_3 = 15;  
    public static final int V4_1 = 16;  
    public static final int V4_2 = 17;  
    public static final int V4_3 = 18;  
    public static final int V4_4 = 19;  
  
    /** 
     *  
     * @Description: 检测当前的版本信息 
     * @param 
     * @return int 
     * @throws 
     */  
    public static int getSystemVersion() {  
        return android.os.Build.VERSION.SDK_INT;  
    }  
  
}  

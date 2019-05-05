package org.hootina.platform;

import android.app.Application;
import android.content.Context;

import com.lidroid.xutils.DbUtils;
import com.nostra13.universalimageloader.cache.memory.impl.LruMemoryCache;
import com.nostra13.universalimageloader.core.ImageLoader;
import com.nostra13.universalimageloader.core.ImageLoaderConfiguration;
import com.nostra13.universalimageloader.core.assist.QueueProcessingType;
import com.tencent.bugly.crashreport.CrashReport;

import org.hootina.platform.db.ChatMsgDb;
import org.hootina.platform.db.DaoSession;
import org.hootina.platform.enums.TabbarEnum;
import org.hootina.platform.result.MemberEntity;
import org.hootina.platform.result.UserServer;
import org.hootina.platform.utils.CrashHandler;
import org.hootina.platform.utils.LoggerFile;
import org.hootina.platform.utils.PictureUtil;

import java.io.File;


public class FlamingoApplication extends Application {
    public static String DEFAULT_APP_PATH = "/sdcard/flamingo";
    public static String DEFAULT_USERS_PATH = "/sdcard/flamingo/Users";
    public static String DEFAULT_LOG_PATH = "/sdcard/flamingo/Logs";

    public static final int CHATMSG_NOTIFICATION_ID = 1;

    private static final String BUGLY_APPID = "315721057f";

    private ChatMsgDb mChatMsgDb;



    private static String mChatServerIp = "120.55.94.78";
    private static short mChatServerPort = 20000;
    private static String mImgServerIp = "120.55.94.78";
    private static short mImgServerPort = 20001;
    private static String mFileServerIp = "120.55.94.78";
    private static short mFileServerPort = 20002;
    private static Context mContext;
    private static int mChatNotificationID = 0;

    private DbUtils db;
    private AppManager appManager;
    private TabbarEnum tabIndex = TabbarEnum.FRIEND;

    //屏幕宽
    private int screenWidth;
    //屏幕高
    private int screenHeight;

    private boolean running = false;

    //用户信息
    private MemberEntity memberEntity;
    private UserServer userServer;


    public static FlamingoApplication instance;

    private DaoSession mDaoSession;

    @Override
    public void onCreate() {
        super.onCreate();

        instance = this;

        mContext = getApplicationContext();

        CrashReport.initCrashReport(getApplicationContext(), BUGLY_APPID, true);

        mChatMsgDb = new ChatMsgDb(this, "mychatdb", null, 1);
        db = DbUtils.create(this);


        initImageLoader();
        CrashHandler crashHandler = CrashHandler.getInstance();
        crashHandler.init(getApplicationContext(), this);

        //创建flamingo根目录
        String path = DEFAULT_APP_PATH;
        File appDir = new File(path);
        if (!appDir.exists()) {
            appDir.mkdir();
        }

        //创建Users目录
        File usersDir = new File(DEFAULT_USERS_PATH);
        if(!usersDir.exists()){
            usersDir.mkdir();
        }

        //创建Logs目录
        File logsDir = new File(DEFAULT_LOG_PATH);
        if(!logsDir.exists()){
            logsDir.mkdir();
        }

        LoggerFile.Init(true);
        LoggerFile.LogInfo("FlamingApplication initialization completed");

        // 创建 bitmapUtils
        PictureUtil.Init();
        PictureUtil.CreateBitmapUtils(this);
    }

    public static Context getContext() {
        return mContext;
    }

    public synchronized static int getChatNotificationID() {
        ++mChatNotificationID;
        return mChatNotificationID;
    }

    public void setRunning(boolean flag) {
        running = flag;
    }

    public boolean getRunning() {
        return running;
    }

    private void initImageLoader() {

        //see: http://www.cnblogs.org/kissazi2/p/3886563.html
        ImageLoaderConfiguration config = new ImageLoaderConfiguration.Builder(
                getApplicationContext()).threadPoolSize(3)
                .threadPriority(Thread.NORM_PRIORITY - 2)
                .tasksProcessingOrder(QueueProcessingType.FIFO)
                .denyCacheImageMultipleSizesInMemory()
                .memoryCache(new LruMemoryCache(2 * 1024 * 1024))
                .memoryCacheSize(2 * 1024 * 1024).memoryCacheSizePercentage(13)
                .diskCacheSize(50 * 1024 * 1024).diskCacheFileCount(100)
                .writeDebugLogs().build();
        ImageLoader.getInstance().init(config);
    }

    public AppManager getAppManager() {
        if (appManager == null) {
            appManager = new AppManager();
        }
        return appManager;
    }

    public synchronized TabbarEnum getTabIndex() {
        return tabIndex;
    }

    public synchronized void setTabIndex(TabbarEnum tabIndex) {
        this.tabIndex = tabIndex;
    }


    public int getScreenWidth() {
        return screenWidth;
    }

    public void setScreenWidth(int screenWidth) {
        this.screenWidth = screenWidth;
    }

    public int getScreenHeight() {
        return screenHeight;
    }

    public void setScreenHeight(int screenHeight) {
        this.screenHeight = screenHeight;
    }

    //聊天服务器
    public static void setChatServerIp(String ip) {
        mChatServerIp = ip;
    }

    public static String getChatServerIp() {
        return mChatServerIp;
    }

    public static void setChatServerPort(short port) {
        mChatServerPort = port;
    }

    public static short getChatServerPort() {
        return mChatServerPort;
    }

    //图片服务器
    public static void setImgServerIp(String ip) {
        mImgServerIp = ip;
    }

    public static String getImgServerIp() {
        return mImgServerIp;
    }

    public static void setImgServerPort(short port) {
        mImgServerPort = port;
    }

    public static short getImgServerPort() {
        return mImgServerPort;
    }

    //文件服务器
    public static void setFileServerIp(String ip) {
        mFileServerIp = ip;
    }

    public static String getFileServerIp() {
        return mFileServerIp;
    }

    public static void setFileServerPort(short port) {
        mFileServerPort = port;
    }

    public static short getFileServerPort() {
        return mFileServerPort;
    }

    public MemberEntity getMemberEntity() {

        if (memberEntity == null) {
            memberEntity = new MemberEntity();
        }

        return memberEntity;
    }

    public ChatMsgDb getChatMsgDb() {
        return mChatMsgDb;
    }

    public DaoSession getDaoSession() {
        return mDaoSession;
    }

    public void setDaoSession(DaoSession daoSession) {
        mDaoSession = daoSession;
    }

    public static FlamingoApplication getInstance() {
        return instance;
    }
}

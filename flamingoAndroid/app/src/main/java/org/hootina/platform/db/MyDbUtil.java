package org.hootina.platform.db;

import android.content.Context;
import android.database.sqlite.SQLiteDatabase;

import org.hootina.platform.FlamingoApplication;

public class MyDbUtil {

    private static ContactsDao mContactsDao;
    private static ChatSessionDao mChatSessionDao;

    public static void initDatabase(Context context, String dbName) {
        DaoMaster.DevOpenHelper helper = new DaoMaster.DevOpenHelper(context, dbName + ".db", null);
        SQLiteDatabase db = helper.getWritableDatabase();
        DaoMaster daoMaster = new DaoMaster(db);
        FlamingoApplication.getInstance().setDaoSession(daoMaster.newSession());
        mContactsDao = FlamingoApplication.getInstance().getDaoSession().getContactsDao();
        mChatSessionDao = FlamingoApplication.getInstance().getDaoSession().getChatSessionDao();
    }

    public static ContactsDao getContactsDao() {
        return mContactsDao;
    }

    public static ChatSessionDao getChatSessionDao() {
        return mChatSessionDao;
    }

    public static void uninit() {
        if (FlamingoApplication.getInstance().getDaoSession() != null) {
            FlamingoApplication.getInstance().getDaoSession().getDatabase().close();
        }

        mChatSessionDao = null;
        mContactsDao = null;
        FlamingoApplication.getInstance().setDaoSession(null);
    }
}

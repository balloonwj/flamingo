package org.hootina.platform.fragments;

import android.content.Intent;
import android.os.Message;
import android.view.ContextMenu;
import android.view.MenuItem;
import android.view.View;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ListView;

import org.hootina.platform.R;
import org.hootina.platform.activities.member.ChattingActivity;
import org.hootina.platform.adapters.ChatSessionAdapter;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.net.ChatSessionMgr;
import org.hootina.platform.result.ChatSession;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;

import java.util.List;

/*
 * @会话列表
 */
public class SessionFragment extends BaseFragment {
    private final int DELETE_SESSION_ITEM = 1;
    private ListView lv_sessionlist;
    private ChatSessionAdapter mMsgAdapter;
    private int mSelfID;
    private String msgtexts;
    private String type;
    private List<ChatSession> mCurrentSessions;

    @Override
    protected int getContentView() {
        return R.layout.activity_message;
    }

    @Override
    protected void initData(View view) {
        //lv_sessionlist = (ListView)findViewById(R.id.tv_server);
        mSelfID = UserSession.getInstance().loginUser.get_userid();

        mCurrentSessions = ChatSessionMgr.getInstance().getAllSessions();
        if (mCurrentSessions == null)
            return;

        if (mMsgAdapter == null) {
            mMsgAdapter = new ChatSessionAdapter(getActivity(), mCurrentSessions);
            lv_sessionlist.setAdapter(mMsgAdapter);
        } else {
            mMsgAdapter.setSessionList(mCurrentSessions);
            mMsgAdapter.notifyDataSetChanged();
        }
    }

    public void refreshSessionList() {
        if (lv_sessionlist == null)
            return;

        mCurrentSessions = ChatSessionMgr.getInstance().getAllSessions();
        if (mCurrentSessions == null)
            return;

        if (mMsgAdapter == null) {
            mMsgAdapter = new ChatSessionAdapter(getActivity(), mCurrentSessions);
            lv_sessionlist.setAdapter(mMsgAdapter);
        } else {
            mMsgAdapter.setSessionList(mCurrentSessions);
            mMsgAdapter.notifyDataSetChanged();
        }
    }

//	@Override
//	public void onHiddenChanged(boolean hidden) {
//		super.onHiddenChanged(hidden);
//		if (hidden) {
//			mCurrentSessions = ChatSessionMgr.getInstance().getAllSessions();
//			mMsgAdapter.notifyDataSetChanged();
//		}
//	}

    @Override
    protected void setData() {
        lv_sessionlist.setOnItemClickListener(new OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view,
                                    int position, long id) {

                if (mCurrentSessions != null && mCurrentSessions.size() > (position)) {
                    ChatSession user = mCurrentSessions.get(position);
                    //int selfID = UserSession.getInstance().loginUser.get_userid();
                    if (user == null)
                        return;

                    Intent intent = new Intent(getActivity(), ChattingActivity.class);

                    intent.putExtra("nickname", user.getFriendNickName());
                    intent.putExtra("userid", user.getmSelfID());
                    intent.putExtra("msgtexts", "");
                    intent.putExtra("type", "");
                    if (UserInfo.isGroup(user.getSelfID())) {
                        intent.putExtra("selfid", user.getFriendID());
                        intent.putExtra("friendid", user.getSelfID());
                    } else {
                        intent.putExtra("selfid", user.getSelfID());
                        intent.putExtra("friendid", user.getFriendID());
                    }

                    getActivity().startActivity(intent);
                }
            }

        });

        // 添加长按点击弹出选择菜单
        lv_sessionlist.setOnCreateContextMenuListener(new View.OnCreateContextMenuListener() {
            public void onCreateContextMenu(ContextMenu menu, View v,
                                            ContextMenu.ContextMenuInfo menuInfo) {
                menu.setHeaderTitle("选择操作");
                menu.add(0, DELETE_SESSION_ITEM, 0, "删除会话");
            }
        });
    }

    //给菜单项添加事件
    @Override
    public boolean onContextItemSelected(MenuItem item) {
        AdapterView.AdapterContextMenuInfo info = (AdapterView.AdapterContextMenuInfo) item.getMenuInfo();
        //info.id得到listview中选择的条目绑定的id
        switch (item.getItemId()) {
            case DELETE_SESSION_ITEM:
                ChatSessionMgr.getInstance().deleteSession((int) info.id);
                refreshSessionList();
                return true;

            default:
                return super.onContextItemSelected(item);
        }

    }

    @Override
    protected void processLogic() {

    }

    @Override
    public void onClick(View v) {

    }

    @Override
    public void onResume() {
        super.onResume();

        refreshSessionList();
    }

    @Override
    public void processMessage(Message msg) {
        super.processMessage(msg);
        if (msg.what == MsgType.msg_type_chat) {

            int senderID = msg.arg1;
//            UserInfo userinfo = UserSession.getInstance().getUserInfoById(senderID);
//            if (userinfo == null)
//                return;


            mCurrentSessions = ChatSessionMgr.getInstance().getAllSessions();
            if (mMsgAdapter == null) {
                mMsgAdapter = new ChatSessionAdapter(getActivity(), mCurrentSessions);
                lv_sessionlist.setAdapter(mMsgAdapter);
            } else {
                mMsgAdapter.setSessionList(mCurrentSessions);
            }

            mMsgAdapter.notifyDataSetChanged();
            lv_sessionlist.setSelection(mMsgAdapter.getCount());
        }
    }

    public void deleteSessionItem(int sessionID) {

    }
}

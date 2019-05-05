package org.hootina.platform.activities.member;

import android.os.Message;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.bumptech.glide.Glide;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.net.NetWorker;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.widgets.x.XListView.IXListViewListener;

import java.util.List;

/*
 * 添加好友
 */
public class AddFriendActivity extends BaseActivity implements IXListViewListener {
    private static final int TAB_TYPE_SINGLE = 0;
    private static final int TAB_TYPE_GROUP  = 1;

    private LinearLayout        ll_person, ll_group;
    private ImageView           iv_group, iv_person;
    private EditText            et_addSearch;
    private TextView            tv_search, tv_person, tv_group;
    //    private String userId;
//    private TargetInfo targetinfo;
//    private SearchFriendAdapter SearcAdapter;
//    private XListView xlv_searchfriend;
//    private int uTargetID;
    //private String number;
    //类型，1 好友，2 群
    private int tabType = TAB_TYPE_SINGLE;

    private RecyclerView        recycler;
    private SearchUserAdapter   mAdapter;
    //private SearchUserResult    mSearchUserResult;
    //查找用户返回的结果
    private List<UserInfo>   findUserResult;

    @Override
    public void onClick(View v) {
        String searchAccount = et_addSearch.getText().toString().trim();
        switch (v.getId()) {
            case R.id.btn_back:
                finish();
                break;

            case R.id.tv_search:
                if (searchAccount.isEmpty() || searchAccount.contains(" ") || searchAccount.length() > 16) {
                    Toast.makeText(AddFriendActivity.this, "输入用户账号无效", Toast.LENGTH_SHORT).show();
                    return;
                }

                NetWorker.searchPersonOrGroup(tabType, searchAccount);
                break;

            case R.id.ll_person:
                if (tabType == TAB_TYPE_SINGLE)
                    return;

                et_addSearch.setText("");
                // 好友
                tabType = TAB_TYPE_SINGLE;
                cleanSelectedStatus();
                tv_person.setTextColor(getResources().getColor(R.color.dark_gray_text));
                iv_person.setBackgroundColor(getResources().getColor(R.color.green));
                break;

            case R.id.ll_group:
                if (tabType == TAB_TYPE_GROUP)
                    return;

                // 群
                et_addSearch.setText("");
                tabType = TAB_TYPE_GROUP;
                cleanSelectedStatus();
                tv_group.setTextColor(getResources().getColor(R.color.dark_gray_text));
                iv_group.setBackgroundColor(getResources().getColor(R.color.green));
                break;

            default:
                break;
        }
    }

    /**
     * 清除选中状态
     */
    private void cleanSelectedStatus() {
        tv_person.setTextColor(getResources().getColor(R.color.gray_text));
        tv_group.setTextColor(getResources().getColor(R.color.gray_text));
        iv_person.setBackgroundColor(getResources().getColor(R.color.white));
        iv_group.setBackgroundColor(getResources().getColor(R.color.white));
    }

    @Override
    public void processMessage(Message msg) {
        super.processMessage(msg);
        if (msg.what == MsgType.msg_type_finduser) {
            findUserResult =  (List<UserInfo>)msg.obj;
            mAdapter.notifyDataSetChanged();
            if (findUserResult.isEmpty()) {
                Toast.makeText(this,"搜索的用户不存在", Toast.LENGTH_SHORT).show();
            }
        } else if (msg.what == MsgType.msg_type_operatefriend) {//添加好友已发送
            if (msg.arg1 == 3) {
                NetWorker.getFriendList();
                //Toast.makeText(this,"添加好友成功",Toast.LENGTH_SHORT).show();
            }
        } else if (msg.what == MsgType.msg_type_getfriendlist) {
			if (msg.arg1 != MsgType.ERROR_CODE_SUCCESS)
				return;

            //FriendList.setFriendList((FriendList) msg.obj);
            Toast.makeText(this,"添加好友/群成功",Toast.LENGTH_SHORT).show();
        }

    }

    @Override
    protected int getContentView() {
        return R.layout.activity_addfriends;
    }

    @Override
    protected void initData() {
        tv_search.setOnClickListener(this);
        ll_person.setOnClickListener(this);
        ll_group.setOnClickListener(this);

        recycler.setLayoutManager(new LinearLayoutManager(this));
        mAdapter = new SearchUserAdapter();
        recycler.setAdapter(mAdapter);
    }

    @Override
    protected void setData() {

    }

    @Override
    public void onRefresh() {

    }

    @Override
    public void onLoadMore() {

    }

    public class SearchUserAdapter extends RecyclerView.Adapter<SearchUserAdapter.SearchUserViewHolder> {

        private LayoutInflater mLayoutInflater;

        @Override
        public SearchUserViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            if (mLayoutInflater == null) {
                mLayoutInflater = LayoutInflater.from(parent.getContext());
            }

            View view = mLayoutInflater.inflate(R.layout.item_search_user, parent, false);

            return new SearchUserViewHolder(view);
        }

        @Override
        public void onBindViewHolder(SearchUserViewHolder holder, int position) {
            holder.bindDataToView(findUserResult.get(position));
        }

        @Override
        public int getItemCount() {
            if (findUserResult == null || findUserResult.isEmpty()) {
                return 0;
            }

            return findUserResult.size();
        }

        public class SearchUserViewHolder extends RecyclerView.ViewHolder {
            private ImageView   mImgHead;
            private TextView    mTvUsername;
            private TextView    mTvNickname;
            private Button      mBtnAdd;

            public SearchUserViewHolder(View itemView) {
                super(itemView);

                mImgHead = (ImageView) itemView.findViewById(R.id.img_head);
                mTvUsername = (TextView) itemView.findViewById(R.id.tv_window_title);
                mTvNickname = (TextView) itemView.findViewById(R.id.tv_nickname);
                mBtnAdd = (Button) itemView.findViewById(R.id.btn_add);
            }

            public void bindDataToView(final UserInfo result) {
                int userid = result.get_userid();
                if (UserInfo.isGroup(userid))
                    Glide.with(AddFriendActivity.this)
                            .load("file:///android_asset/head" + 100 + ".png")
                            .into(mImgHead);
                else
                    Glide.with(AddFriendActivity.this)
                            .load("file:///android_asset/head" + result.get_faceType() + ".png")
                            .into(mImgHead);

                mTvUsername.setText(result.get_username());
                mTvNickname.setText(result.get_nickname());

                UserSession userSession = UserSession.getInstance();
                if (!userSession.isFriend(userid) && !userSession.isSelf(userid)) {
                    if (UserInfo.isGroup(userid))
                        mBtnAdd.setText("申请加群");
                    else
                        mBtnAdd.setText("添加好友");

                    mBtnAdd.setOnClickListener(new View.OnClickListener() {
                        @Override
                        public void onClick(View v) {
                            NetWorker.addFriend(result.get_userid());
                            mBtnAdd.setText("已发送");
                            mBtnAdd.setTextColor(itemView.getResources().getColor(R.color.gray));
                        }
                    });
                }
            }
        }
    }

}

package org.hootina.platform.activities.member;

import android.content.Intent;
import android.os.Bundle;
import android.os.Message;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import com.bumptech.glide.Glide;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.details.GroupDetailActivity;
import org.hootina.platform.dialogs.BaseDialog;
import org.hootina.platform.dialogs.LiteDialog;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.model.GroupInfo;
import org.hootina.platform.model.GroupInfos;
import org.hootina.platform.net.NetWorker;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.utils.HeadUtil;

import java.util.List;

public class GroupListActivity extends BaseActivity {
    private List<UserInfo>      groups;
    private RecyclerView        mRecyclerView;
    private GroupListAdapter    mGroupListAdapter;
    private ImageView           mImgAdd;

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.btn_back:
                finish();
                break;

            default:
                break;
        }
    }

    @Override
    protected int getContentView() {
        return R.layout.activity_group;
    }

    @Override
    protected void initData() {
        loadGrouplist();
    }

    @Override
    protected void onCreate(Bundle bundle) {
        super.onCreate(bundle);

        mRecyclerView = (RecyclerView) findViewById(R.id.recycler);
        mGroupListAdapter = new GroupListAdapter();

        mRecyclerView.setLayoutManager(new LinearLayoutManager(this));
        mRecyclerView.setAdapter(mGroupListAdapter);

        mImgAdd = (ImageView) findViewById(R.id.img_add);

        mImgAdd.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(final View item) {
                LiteDialog.instance()
                        .layoutId(R.layout.dialog_add_group)
                        .width(220)
                        .height(BaseDialog.WRAP_CONTENT)
                        .initViewListener(new BaseDialog.InitViewListener() {
                            @Override
                            public void initView(View view, final BaseDialog dialog) {
                                TextView txtCreateGroup = (TextView) view.findViewById(R.id.txt_creategroup);
                                TextView txtCancel = (TextView) view.findViewById(R.id.txt_cancle);
                                final EditText editText = (EditText) view.findViewById(R.id.edit);

                                txtCancel.setOnClickListener(new View.OnClickListener() {
                                    @Override
                                    public void onClick(View view) {
                                        dialog.dismiss();
                                    }
                                });

                                txtCreateGroup.setOnClickListener(new View.OnClickListener() {
                                    @Override
                                    public void onClick(View click) {
                                        String name = editText.getText().toString();
                                        if (name == null || name.equals("") || name.contains(" ") || name.length() > 16) {
                                            Toast.makeText(item.getContext(), "无效的群名称", Toast.LENGTH_SHORT).show();
                                            return;
                                        } else {
                                            NetWorker.createGroup(name);
                                        }

                                        dialog.dismiss();
                                    }
                                });
                            }
                        })
                        .show(getSupportFragmentManager());
            }
        });

        //NetWorker.getFriendList();
    }

    private void loadGrouplist() {
        groups = UserSession.getInstance().groups;
    }

    @Override
    protected void setData() {
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    protected void onResume() {
        super.onResume();

        loadGrouplist();
        mGroupListAdapter.notifyDataSetChanged();
    }

    @Override
    public void processMessage(Message msg) {
            super.processMessage(msg);

        if (msg.what == MsgType.msg_type_getfriendlist) {
			if (msg.arg1 != MsgType.ERROR_CODE_SUCCESS)
				return;

            loadGrouplist();

            mGroupListAdapter.notifyDataSetChanged();
        } else if (msg.what == MsgType.msg_type_getgroupmembers) {
            GroupInfo info = (GroupInfo) msg.obj;
            GroupInfos.getInstance().getGroupInfos().add(info);

            for (GroupInfo.MembersBean membersBean : info.getMembers()) {
                HeadUtil.put(membersBean.getUserid(), membersBean.getFacetype());
            }

            mGroupListAdapter.notifyDataSetChanged();
        } else if (msg.what == MsgType.msg_type_creategroup) {
            if (msg.arg1 == MsgType.ERROR_CODE_CREATEGROUPFAILED) {
                Toast.makeText(this, "群创建失败", Toast.LENGTH_SHORT).show();
                return;
            }

            NetWorker.getFriendList();

            Toast.makeText(this, "群创建成功", Toast.LENGTH_SHORT).show();
        }
    }

    public class GroupListAdapter extends RecyclerView.Adapter<GroupListAdapter.GroupViewHolder> {
        @Override
        public GroupListAdapter.GroupViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.friend_item, parent, false);
            return new GroupListAdapter.GroupViewHolder(view);
        }

        @Override
        public void onBindViewHolder(GroupListAdapter.GroupViewHolder holder, int position) {
            if (position >= groups.size())
                return;

            holder.bindDataToView(groups.get(position));
        }

        @Override
        public int getItemCount() {
            if (groups == null)
                return 0;

            return groups.size();
        }

        public class GroupViewHolder extends RecyclerView.ViewHolder {
            private ImageView mImgHead;
            private TextView  mTxtName;
            private TextView  mTxtSign;

            public GroupViewHolder(View itemView) {
                super(itemView);

                mImgHead = (ImageView) itemView.findViewById(R.id.img_head);
                mTxtName = (TextView) itemView.findViewById(R.id.tv_window_title);
                mTxtSign = (TextView) itemView.findViewById(R.id.txt_sign);
            }

            public void bindDataToView(final UserInfo result) {
                Glide.with(GroupListActivity.this)
                        .load("file:///android_asset/head" + 100 + ".png")
                        .into(mImgHead);

                mTxtName.setText(result.get_nickname() + "(" + result.get_userid() + ")");

                itemView.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View v) {
                        Intent intent = new Intent(GroupListActivity.this, GroupDetailActivity.class);
                        intent.putExtra("groupName", result.get_nickname());
                        intent.putExtra("groupID", result.get_userid());

                        startActivityForResult(intent, 0);
                    }
                });

                HeadUtil.put(result.get_userid(), result.get_faceType());
            }
        }
    }
}

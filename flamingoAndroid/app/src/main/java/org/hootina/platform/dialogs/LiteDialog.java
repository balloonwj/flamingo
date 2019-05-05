package org.hootina.platform.dialogs;

import android.view.View;

/**
 * Created by runningzou on 2017/11/28.
 * 通过 builder 模式实现不同效果
 */

public class LiteDialog extends BaseDialog {

    @Override
    public int intLayoutId() {
        if (mLayoutId == 0) {
            throw  new RuntimeException("you must invoke layoutId()");
        }
        return mLayoutId;
    }

    @Override
    public void initView(View view) {

    }

    public static LiteDialog instance() {
        return new LiteDialog();
    }

    public LiteDialog layoutId(int layoutId) {
        mLayoutId = layoutId;
        return this;
    }

}

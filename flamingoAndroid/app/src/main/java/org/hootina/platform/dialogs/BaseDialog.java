package org.hootina.platform.dialogs;

import android.content.DialogInterface;
import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;
import android.support.annotation.LayoutRes;
import android.support.annotation.Nullable;
import android.support.annotation.StyleRes;
import android.support.v4.app.DialogFragment;
import android.support.v4.app.Fragment;
import android.support.v4.app.FragmentManager;
import android.support.v4.app.FragmentTransaction;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;

import org.hootina.platform.R;
import org.hootina.platform.utils.DimenUtil;

import java.util.List;

/**
 * Created by runningzou on 2017/11/24.
 * 通过继承来实现不同的效果
 */

public abstract class BaseDialog extends DialogFragment {

    public static final int MATCH_PARENT = -1;
    public static final int WRAP_CONTENT = -2;

    private static final String INIT_LISTENER = "init_listener";
    private static final String MARGIN = "margin";
    private static final String WIDTH = "width";
    private static final String HEIGHT = "height";
    private static final String DIM = "dim_amount";
    private static final String BOTTOM = "show_bottom";
    private static final String CANCEL = "out_cancel";
    private static final String ANIM = "anim_style";
    private static final String LAYOUT = "layout_id";

    private int mMargin;//左右边距
    private int mWidth;//宽度
    private int mHeight;//高度
    private float mDimAmount = 0.5f;//灰度深浅
    private boolean mShowBottom;//是否底部显示
    private boolean mOutCancel = true;//是否点击外部取消
    @StyleRes
    private int mAnimStyle;
    @LayoutRes
    protected int mLayoutId = 0;
    private InitViewListener mListener;
    private DialogInterface.OnCancelListener mOnCancelListener;

    public abstract int intLayoutId();

    public abstract void initView(View view);

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setStyle(DialogFragment.STYLE_NO_TITLE, R.style.base_dialog);

        mLayoutId = intLayoutId();
        //恢复保存的数据
        if (savedInstanceState != null) {
            mMargin = savedInstanceState.getInt(MARGIN);
            mWidth = savedInstanceState.getInt(WIDTH);
            mHeight = savedInstanceState.getInt(HEIGHT);
            mDimAmount = savedInstanceState.getFloat(DIM);
            mShowBottom = savedInstanceState.getBoolean(BOTTOM);
            mOutCancel = savedInstanceState.getBoolean(CANCEL);
            mAnimStyle = savedInstanceState.getInt(ANIM);
            mLayoutId = savedInstanceState.getInt(LAYOUT);
            mListener = savedInstanceState.getParcelable(INIT_LISTENER);
        }

    }

    @Nullable
    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {

        View view = inflater.inflate(intLayoutId(), container, false);
        initView(view);

        if (mListener != null) {
            mListener.initView(view,this);
        }

        return view;
    }

    @Override
    public void onStart() {
        super.onStart();
        initParams();
    }

    /**
     * 屏幕旋转等导致DialogFragment销毁后重建时保存数据
     *
     * @param outState
     */
    @Override
    public void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        outState.putInt(MARGIN, mMargin);
        outState.putInt(WIDTH, mWidth);
        outState.putInt(HEIGHT, mHeight);
        outState.putFloat(DIM, mDimAmount);
        outState.putBoolean(BOTTOM, mShowBottom);
        outState.putBoolean(CANCEL, mOutCancel);
        outState.putInt(ANIM, mAnimStyle);
        outState.putInt(LAYOUT, mLayoutId);
        outState.putParcelable(INIT_LISTENER, mListener);
    }

    private void initParams() {
        Window window = getDialog().getWindow();
        if (window != null) {
            WindowManager.LayoutParams lp = window.getAttributes();
            //调节灰色背景透明度[0-1]，默认0.5f
            lp.dimAmount = mDimAmount;
            //是否在底部显示
            if (mShowBottom) {
                lp.gravity = Gravity.BOTTOM;
                if (mAnimStyle == 0) {
                    mAnimStyle = R.style.default_animation;
                }
            }

            //设置dialog宽度
            if (mWidth == 0) { //未设置宽度
                lp.width = DimenUtil.getScreenWidth() - 2 * DimenUtil.dp2px(mMargin);
            } else if (mWidth == MATCH_PARENT) {
                lp.width = WindowManager.LayoutParams.MATCH_PARENT;
            } else if (mWidth == WRAP_CONTENT) {
                lp.width = WindowManager.LayoutParams.WRAP_CONTENT;
            } else {
                lp.width = DimenUtil.dp2px(mWidth);
            }

            //设置dialog高度
            if (mHeight == 0 || mHeight == WRAP_CONTENT) { //未设置高度或 WRAP_CONTENT
                lp.height = WindowManager.LayoutParams.WRAP_CONTENT;
            } else if (mHeight == MATCH_PARENT) {
                lp.height = WindowManager.LayoutParams.MATCH_PARENT;
            } else {
                lp.height = DimenUtil.dp2px(mHeight);
            }

            //设置dialog进入、退出的动画
            window.setWindowAnimations(mAnimStyle);
            window.setAttributes(lp);
        }
        setCancelable(mOutCancel);
    }

    public BaseDialog margin(int margin) {
        this.mMargin = margin;
        return this;
    }

    public BaseDialog width(int width) {
        this.mWidth = width;
        return this;
    }

    public BaseDialog height(int height) {
        this.mHeight = height;
        return this;
    }

    public BaseDialog dimAmount(float dimAmount) {
        this.mDimAmount = dimAmount;
        return this;
    }

    public BaseDialog showBottom(boolean showBottom) {
        this.mShowBottom = showBottom;
        return this;
    }

    public BaseDialog outCancel(boolean outCancel) {
        this.mOutCancel = outCancel;
        return this;
    }

    public BaseDialog animStyle(@StyleRes int animStyle) {
        this.mAnimStyle = animStyle;
        return this;
    }

    public BaseDialog onCancleListner(DialogInterface.OnCancelListener listener) {
        mOnCancelListener = listener;
        return this;
    }


    public BaseDialog initViewListener(InitViewListener listener) {
        mListener = listener;
        return this;
    }

    public boolean isShowing() {
        if (getDialog() != null) {
            return getDialog().isShowing();
        }

        return false;
    }


    @Override
    public void onCancel(DialogInterface dialog) {
        super.onCancel(dialog);
        if (mOnCancelListener != null) {
            mOnCancelListener.onCancel(dialog);
        }
    }

    @Override
    public void dismiss() {
        if (isShowing()) {
            super.dismissAllowingStateLoss();
        }
    }

    public BaseDialog show(FragmentManager manager) {

        List<Fragment> fragments = manager.getFragments();

//        if (fragments.contains(this)) {
//            return this;
//        }

        if (fragments != null) {
            FragmentTransaction ft = manager.beginTransaction();
            for (Fragment fragment : fragments) {
                if (fragment != null && fragment.isAdded() && fragment instanceof BaseDialog) {
                    ft.remove(fragment);
                }
            }
            ft.commitAllowingStateLoss();
        }

        FragmentTransaction ft = manager.beginTransaction();
        ft.add(this, String.valueOf(System.currentTimeMillis()));
        ft.commitAllowingStateLoss();

        return this;
    }


    public static abstract class InitViewListener implements Parcelable {

        public abstract void initView(View view, BaseDialog baseDialog);


        @Override
        public int describeContents() {
            return 0;
        }

        @Override
        public void writeToParcel(Parcel dest, int flags) {
        }

        public InitViewListener() {
        }

        protected InitViewListener(Parcel in) {
        }

        public static final Creator<InitViewListener> CREATOR = new Creator<InitViewListener>() {
            @Override
            public InitViewListener createFromParcel(Parcel source) {
                return null;
            }

            @Override
            public InitViewListener[] newArray(int size) {
                return new InitViewListener[size];
            }
        };
    }
}
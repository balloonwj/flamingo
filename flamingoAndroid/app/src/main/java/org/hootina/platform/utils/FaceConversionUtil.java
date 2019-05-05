package org.hootina.platform.utils;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.TextUtils;
import android.text.style.ImageSpan;
import android.util.Log;

import org.hootina.platform.R;
import org.hootina.platform.activities.ChatEmoji;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class FaceConversionUtil {
    /** 每一页表情的个数 */
    private int pageSize = 20;

    private static FaceConversionUtil mFaceConversionUtil;

    /** 保存于内存中的表情HashMap */
    private HashMap<String, String> emojiMap = new HashMap<String, String>();

    /** 保存于内存中的表情集合 */
    private List<ChatEmoji> emojis = new ArrayList<ChatEmoji>();

    /** 表情分页的结果集合 */
    public List<List<ChatEmoji>> emojiLists = new ArrayList<List<ChatEmoji>>();

    private Map<String,Integer> emojiNameIdMap = new HashMap<>();

    private FaceConversionUtil() {

    }

    public static FaceConversionUtil getInstace() {
        if (mFaceConversionUtil == null) {
            mFaceConversionUtil = new FaceConversionUtil();
        }
        return mFaceConversionUtil;
    }

    /**
     * 得到一个SpanableString对象，通过传入的字符串,并进行正则判断
     *
     * @param context
     * @param str
     * @return
     */
    public SpannableString getExpressionString(Context context, String str) {
        SpannableString spannableString = new SpannableString(str);
        //		Toast t=Utils.showToast(context, str, Toast.LENGTH_LONG);
        //		t.show();
        // 正则表达式比配字符串里是否含有表情，如： 我好[开心]啊
        //		String zhengze = "\\[[^\\]]+\\]";
        String zhengze = "\\[[^]]+\\]";
        // 通过传入的正则表达式来生成一个pattern
        Pattern sinaPatten = Pattern.compile(zhengze, Pattern.CASE_INSENSITIVE);
        try {
            dealExpression(context, spannableString, sinaPatten, 0);
        } catch (Exception e) {
            Log.e("dealExpression", e.getMessage());
        }
        return spannableString;
    }

    /**
     * 添加表情
     *
     * @param context
     * @param imgId
     * @param spannableString
     * @return
     */
    public SpannableString addFace(Context context, int imgId,
                                   String spannableString) {
        if (TextUtils.isEmpty(spannableString)) {
            return null;
        }
        Bitmap bitmap = BitmapFactory.decodeResource(context.getResources(),
                imgId);
        bitmap = Bitmap.createScaledBitmap(bitmap, 60, 60, true);
        ImageSpan imageSpan = new ImageSpan(context, bitmap);
        SpannableString spannable = new SpannableString(spannableString);
        spannable.setSpan(imageSpan, 0, spannableString.length(),
                Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        return spannable;
    }

    /**
     * 添加图片
     * @param context
     * @param bmpUrl 图片路径
     * @param spannableString
     * @return
     */
    public SpannableString addPicture(Context context, String bmpUrl,
                                      String spannableString) {
        if (TextUtils.isEmpty(spannableString)) {
            return null;
        }
        //		Bitmap bitmap = BitmapFactory.decodeResource(context.getResources(),
        //				imgId);
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = 4;
        Bitmap bitmap =BitmapFactory.decodeFile(bmpUrl,options);
        bitmap = Bitmap.createScaledBitmap(bitmap, 120, 120, true);//图片压缩

        ImageSpan imageSpan = new ImageSpan(context, bitmap);
        SpannableString spannable = new SpannableString(spannableString);
        spannable.setSpan(imageSpan, 0, spannableString.length(),
                Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        return spannable;
    }

    /**
     * 对spanableString进行正则判断，如果符合要求，则以表情图片代替
     *
     * @param context
     * @param spannableString
     * @param patten
     * @param start
     * @throws Exception
     */
    private void dealExpression(Context context,SpannableString spannableString, Pattern patten, int start)
            throws Exception {
        Matcher matcher = patten.matcher(spannableString);
        while (matcher.find()) {
            //在这里判断如果是本地连接就去本地加载图片，
            //如果是图片则显示图片，
            //如果是网络图片这异步加载图片
            String key = matcher.group();
            //Log.d("TAG", "key........................."+key);
            //Log.d("TAG", "key........................."+key.length());
            if(key.length()<7){
                //Log.d("TAG", "发送的是表情");
                // 返回第一个字符的索引的文本匹配整个正则表达式,ture 则继续递归
                //Log.d("TAG", "matcher.start()"+matcher.start());
                if (matcher.start() < start) {
                    //Log.d("TAG", "发送的是表情continue");
                    continue;
                }
                //Log.d("TAG", "发送的是表情");
                String value = emojiMap.get(key);
                if (TextUtils.isEmpty(value)) {
                    continue;
                }
                int resId = context.getResources().getIdentifier(value, "drawable",
                        context.getPackageName());
                // 通过上面匹配得到的字符串来生成图片资源id
                if (resId != 0) {
                    Bitmap bitmap = BitmapFactory.decodeResource(context.getResources(), resId);
                    bitmap = Bitmap.createScaledBitmap(bitmap, 100, 100, true);
                    // 通过图片资源id来得到bitmap，用一个ImageSpan来包装
                    ImageSpan imageSpan = new ImageSpan(bitmap);
                    // 计算该图片名字的长度，也就是要替换的字符串的长度
                    int end = matcher.start() + key.length();
                    // 将该图片替换字符串中规定的位置中
                    spannableString.setSpan(imageSpan, matcher.start(), end,Spannable.SPAN_INCLUSIVE_EXCLUSIVE);
                    if (end < spannableString.length()) {
                        // 如果整个字符串还未验证完，则继续。。
                        dealExpression(context, spannableString, patten, end);
                    }
                    break;
                }
            }else{
                //Log.d("TAG", "发送的不是表情");
                // 返回第一个字符的索引的文本匹配整个正则表达式,ture 则继续递归
                if (matcher.start() < start) {
                    continue;
                }
                //Log.d("TAG", "发送的不是表情");
                BitmapFactory.Options options = new BitmapFactory.Options();
                options.inSampleSize = 4;
                //Log.d("TAG", "key"+key.substring(1, key.length()-1));
                Bitmap bitmap =BitmapFactory.decodeFile((key.substring(1, key.length()-1)),options);
                bitmap = Bitmap.createScaledBitmap(bitmap, 450, 450, true);//图片压缩
                //Log.d("TAG", "发送的不是表情"+bitmap);
                // 通过图片资源id来得到bitmap，用一个ImageSpan来包装
                ImageSpan imageSpan = new ImageSpan(bitmap);
                // 计算该图片名字的长度，也就是要替换的字符串的长度
                int end = matcher.start() + key.length();
                // 将该图片替换字符串中规定的位置中
                spannableString.setSpan(imageSpan, matcher.start(), end,
                        Spannable.SPAN_INCLUSIVE_EXCLUSIVE);
                if (end < spannableString.length()) {
                    // 如果整个字符串还未验证完，则继续。。
                    dealExpression(context, spannableString, patten, end);
                }
                break;
            }
        }
    }

    public void getFileText(Context context) {
        ParseData(UIUtils.FileUtils.getEmojiFile(context), context);
    }

    /**
     * 解析字符
     *
     * @param data
     */
    private void ParseData(List<String> data, Context context) {
        if (data == null) {
            return;
        }
        ChatEmoji emojEentry;
        try {
            for (String str : data) {
                String[] text = str.split(",");
                String fileName = text[0].substring(0, text[0].lastIndexOf("."));
                emojiMap.put(text[1], fileName);
                int resID = context.getResources().getIdentifier(fileName,"drawable", context.getPackageName());

                if (resID != 0) {
                    emojEentry = new ChatEmoji();
                    emojEentry.setId(resID);
                    emojEentry.setCharacter(text[1]);
                    emojEentry.setFaceName(fileName);
                    emojis.add(emojEentry);
                    emojiNameIdMap.put(parseEmojNum(text[1]),resID);
                }
            }
            int pageCount = (int) Math.ceil(emojis.size() / 20 + 0.1);

            for (int i = 0; i < pageCount; i++) {
                emojiLists.add(getData(i));
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private String parseEmojNum(String str) {
        str = str.replace("|[","");
        return str.replace("]|", "");
    }

    /**
     * 获取分页数据
     *
     * @param page
     * @return
     */
    private List<ChatEmoji> getData(int page) {
        int startIndex = page * pageSize;
        int endIndex = startIndex + pageSize;

        if (endIndex > emojis.size()) {
            endIndex = emojis.size();
        }
        // 不这么写，会在viewpager加载中报集合操作异常，我也不知道为什么
        List<ChatEmoji> list = new ArrayList<ChatEmoji>();
        list.addAll(emojis.subList(startIndex, endIndex));
        if (list.size() < pageSize) {
            for (int i = list.size(); i < pageSize; i++) {
                ChatEmoji object = new ChatEmoji();
                list.add(object);
            }
        }
        if (list.size() == pageSize) {
            ChatEmoji object = new ChatEmoji();
            object.setId(R.drawable.face_del_icon);
            list.add(object);
        }
        return list;
    }


    public int getEmojeId(String name) {
        return emojiNameIdMap.get(name);
    }

}

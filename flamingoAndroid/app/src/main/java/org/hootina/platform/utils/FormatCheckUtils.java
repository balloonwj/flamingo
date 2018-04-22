package org.hootina.platform.utils;

/**
 * Created by lzh on 2015/5/18.
 */
public class FormatCheckUtils {

        /**
         * 判断是否符合邮箱格式
         */
        public static boolean checkEmailValid(String strEmail) {
                if (null == strEmail) {
                        return false;
                }
                return strEmail.matches("[a-zA-Z0-9_]+@[a-z0-9]+(.[a-z]+){2}");
        }


        /**
         * 判断是否符合座机号格式
         *
         * @param phoneNumber
         * @return
         */
        public static boolean checkPhoneNumberValid(String phoneNumber) {
                if (null == phoneNumber) {
                        return false;
                }

                /**
                 * 匹配北京上海等3-8格式：(^0[1,2]{1}\\d{1}-?\\d{8}
                 * 匹配其他省份等4-7/8格式:(^0[3-9]{1}\\d{2}-?\\d{7,8})
                 * 匹配内部电话转接号：(-(\\d{1,4}))?$)
                 */
                // 区号与座机号之间可不添加“-” 外部号码与内部号码之间必须添加“-”
                String check = "((^0[1,2]{1}\\d{1}-?\\d{8}|(^0[3-9]{1}\\d{2}-?\\d{7,8}))(-(\\d{1,4}))?$)";
                return phoneNumber.matches(check);
        }

        /**
         * 验证手机号方法
         *
         * @param strPhoneNum
         * @return
         */
        public static boolean checkMobileNumberValid(String strPhoneNum) {
                if (null == strPhoneNum) {
                        return false;
                }
                /**
                 * 匹配13、15、18开头手机号 排除154 开头手机号
                 * 匹配170、176、177、178开头手机号
                 * 匹配规则参考当前（2015-04-29）百度百科“手机号”罗列号码
                 */
                String checkphone = "^(((13|18)[0-9])|(15[^4,\\D])|170|176|177|178)\\d{8}$";
                return strPhoneNum.matches(checkphone);

        }

}

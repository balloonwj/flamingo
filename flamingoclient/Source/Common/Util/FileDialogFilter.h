/**
 *  文件对话框（GetOpenFileName / GetSaveFileName / WTL CFileDialog）的文件类型过滤串。
 *
 *  过滤串的格式是「说明\0通配符\0说明\0通配符\0\0」，中间夹着 \0，不能整串交给 TR 查找（TR 按 C 字符串查找，
 *  只看得到第一段）。所以说明文字各自用 TR 取当前界面语言的译文，再由这里与通配符拼成过滤串（多语言第 5 期）。
 *  只有头文件，用到的模块直接包含即可。典型用法：
 *      const std::wstring fileFilter = filedlg::MakeFilter(TR(L"图片文件"), filedlg::kImagePatterns,
 *                                                          TR(L"所有文件"), L"*.*");
 *      ofn.lpstrFilter = fileFilter.c_str();   // fileFilter 须活到对话框关闭
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <string>

namespace filedlg {

//常用的图片类型通配符（打开 / 保存图片的对话框共用）
const wchar_t* const kImagePatterns = L"*.png;*.jpg;*.jpeg;*.bmp;*.gif";

//往过滤串末尾追加一项「说明\0通配符\0」；说明或通配符为 NULL 时按空串处理
inline void AppendFilterItem(std::wstring& filter, const wchar_t* desc, const wchar_t* pattern)
{
    filter.append((desc != NULL) ? desc : L"");
    filter.push_back(L'\0');
    filter.append((pattern != NULL) ? pattern : L"");
    filter.push_back(L'\0');
}

/**
 *  拼只有一项的过滤串：「说明\0通配符\0\0」。
 *  @param desc    说明文字（已取译文），如 TR(L"所有文件")
 *  @param pattern 通配符，如 L"*.*"，多个通配符用分号隔开
 *  @return 过滤串；c_str() 交给对话框，返回值须活到对话框关闭
 */
inline std::wstring MakeFilter(const wchar_t* desc, const wchar_t* pattern)
{
    std::wstring filter;
    AppendFilterItem(filter, desc, pattern);
    filter.push_back(L'\0');
    return filter;
}

/**
 *  拼两项的过滤串：「说明1\0通配符1\0说明2\0通配符2\0\0」。对话框默认选中第一项。
 *  @param desc1    第一项说明文字（已取译文）
 *  @param pattern1 第一项通配符
 *  @param desc2    第二项说明文字（已取译文）
 *  @param pattern2 第二项通配符
 *  @return 过滤串；c_str() 交给对话框，返回值须活到对话框关闭
 */
inline std::wstring MakeFilter(const wchar_t* desc1, const wchar_t* pattern1,
                               const wchar_t* desc2, const wchar_t* pattern2)
{
    std::wstring filter;
    AppendFilterItem(filter, desc1, pattern1);
    AppendFilterItem(filter, desc2, pattern2);
    filter.push_back(L'\0');
    return filter;
}

} // namespace filedlg

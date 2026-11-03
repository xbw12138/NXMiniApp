//
// miniapp: 用 QuickJS 执行 index.js，并取出 Page({ data }) 的 JSON 数据。
//

#pragma once

#include <string>

namespace miniapp {

/// 执行页面脚本；成功后 jsonOut 为 pageObj.data 的 JSON 字符串。
bool runPageScript(const std::string& source, const std::string& filename,
                   std::string& jsonOut, std::string& error);

}  // namespace miniapp

// scene_json_alias_test.cpp —— 「tg::Json 不必 include IPC 头即可用」的编译期判据。
//
// 为什么单独一个 TU：`tg::Json` 的别名只在 ipc.hpp 与 scene.hpp 各声明一份，
// 而绝大多数测试经 trogue.hpp（伞头）间接包含了 ipc.hpp ——在那些 TU 里断言
// 「tg::Json 可用」永远成立，证明不了「只用资产头即可拿到 JSON 载体」。
// 本 TU 只 include trogue/scene.hpp：若 scene.hpp 里那行别名被删掉，
// 下面的 static_assert 立即编译失败，判据即本文件的 include 行。
//
// 边界：本判据钉的是「不引 ipc.hpp 也能拿到 tg::Json」，钉不住「scene.hpp
// 日后反向 include ipc.hpp」（那会破坏依赖方向，靠 scene.hpp 别名旁的注释与
// 代码评审把关，不为一行声明引入脚本守卫）。
#include <type_traits>

#include <nlohmann/json.hpp>

#include "trogue/scene.hpp"

static_assert(std::is_same_v<tg::Json, nlohmann::json>,
              "tg::Json 应在 scene.hpp 可达且与 nlohmann::json 同型");

int main() { return 0; }

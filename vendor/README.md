# 第三方源码与项目适配

各文件保留上游版权和许可，完整依赖清单见项目根目录 `THIRD_PARTY.md`。

## libultrahand

基线为 `ppkantorski/libultrahand` 的提交 `2e4df5430045217093dd77751cd9e16c92fe9144`。本仓库直接保留源码，不要求用户更换 NS 上的 Ultrahand 或加载器。

GameLex 对 `libtesla/include/tesla.hpp` 的适配：

- `GAMELEX_CHINESE_FONT`：补充中文字体选择与字体回退，使用设备提供的字体，不附带独立字体文件。
- `GAMELEX_CAPTURE_GUARD`：截图透明阶段暂停通知层绘制，避免工具通知混入 OCR 图片；通知计时继续更新。

这些修改只在构建 GameLex 时启用。其余截图层排除调用位于本项目的浮层代码，使用现有双缓冲与侧栏生命周期，没有修改用户的 UH 主程序。

## 其他依赖

`json.hpp` 及各许可证文本随仓库保留。libjpeg-turbo 本机测试源码和 Mac 工具链由 `scripts/fetch_deps.py` 按 `deps.lock.json` 下载，不提交工具链或构建产物。

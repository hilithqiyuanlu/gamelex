# 第三方依赖

本工程自身代码采用 GPL-2.0-or-later；第三方源文件保留各自许可和作者声明。

| 依赖 | 固定版本／用途 | 许可与来源 |
| --- | --- | --- |
| libultrahand | `2e4df5430045217093dd77751cd9e16c92fe9144`，浮层（含本项目适配，见 `vendor/README.md`） | 保留 `LICENSE`、`SUB_LICENSE` 及各源文件声明，含 GPL 与署名要求；https://github.com/ppkantorski/libultrahand |
| nlohmann/json | 3.11.3，配置和协议 | MIT，`vendor/JSON-LICENSE.MIT`；https://github.com/nlohmann/json |
| libnx | 4.12.0，Switch 接口 | ISC；https://github.com/switchbrew/libnx |
| Switch curl | 官方 Switch port 7.69.1-5，libnx TLS 后端 | curl 许可，已保留上游版权文本；https://github.com/devkitPro/pacman-packages/tree/master/switch/curl |
| libjpeg-turbo | Switch 2.1.2-2；本机测试 3.1.3，逐行 JPEG 裁剪 | BSD、IJG、zlib 等，见原许可；https://github.com/libjpeg-turbo/libjpeg-turbo |
| zlib／MiniZip | Switch zlib 1.3.1-1 包含的库，浮层依赖 | zlib 及 MiniZip 作者声明，原许可随包分发 |
| Mbed TLS | 2.28.10-1，浮层链接依赖 | Apache-2.0；https://github.com/Mbed-TLS/mbedtls |
| GCC／newlib／libstdc++ | 由固定 devkitA64 提供 | 各自许可及 GCC Runtime Library Exception，工具链不随安装包分发 |
| 根证书包 | curl CA Extract，文件包含生成日期和来源 | Mozilla CA 证书数据；https://curl.se/docs/caextract.html |

第一阶段不使用 ECDICT、独立字体或云服务 SDK，相关数据尚未打包。正式离线词典与字体将在实机可行性通过后接入并补全许可。

截图和服务通信流程参考 https://github.com/mariano54/switch-ocr ，不继承其 Mac 服务、Gemini 专用协议、日语处理或 ISSEN 依赖；本工程系统模块的权限声明参考其公开示例后调整，来源项目采用 MIT，原始许可保存在 `vendor/Switch-OCR-LICENSE`。

包的完整源代码随 `gamelex-source.zip` 一并提供；不要只分发 `.ovl` 而遗漏对应源码和许可证。当前发布为 0.3.0 试用版，不作正式稳定版承诺。

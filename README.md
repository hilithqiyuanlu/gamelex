# GameLex

**在游戏里遇到英文，选一段看翻译，再回到原句查词。**

GameLex 是面向第一代 Nintendo Switch 的英语阅读辅助浮层，运行于 **大气层（Atmosphère）+ Ultrahand** 环境。NS 直接联网识别和翻译，不需要电脑常驻，也不替换游戏里的英文。

> 当前版本：**0.3.0 试用版**。核心流程已实现，构建和本机测试通过；0.3.0 的实机稳定性与不同机型、固件、UH／加载器组合仍需验证。目前还没有离线词典和生词本。

[下载试用包](https://github.com/hilithqiyuanlu/gamelex/releases/tag/v0.3.0) · [安装说明](docs/同学试用.md) · [报告问题](https://github.com/hilithqiyuanlu/gamelex/issues) · [模块与发布](docs/模块与发布.md)

## 怎么用

```text
游戏停留在英文画面
        ↓
Ultrahand → GameLex：自动截图识别
        ↓
左侧按位置显示英文文字组
        ↓
选中需要的组 → 显示中文译文
        ↓
切回英文 → 选词或扩选短语 → 查询
        ↓
返回列表，或退出继续游戏
```

- **只翻译你选择的内容。** 识别整屏后先显示英文组，不自动翻译菜单、图标等全部结果。
- **自动分组，可手动纠正。** 根据文字位置、行距、字高和对齐合并；连续正文被拆开时，用 L/R 缩扩相邻组。
- **保留原句。** 中英切换，在原句中选词、扩选相邻词查询；B 返回后保留选择与滚动位置。
- **兼顾手柄和触屏。** 手柄可完成核心流程，掌机也可以点选和拖动。
- **控制重复请求。** 当前截图中缓存范围内已有的译文不重复请求；不自动重试收费接口。
- **独立安装。** 不替换 UH、加载器或大气层；卸载保留设置和数据。

识别不会暂停游戏。建议先用于可停留阅读的对话、任务日志和物品说明，不承诺捕获瞬时字幕。

## 安装前需要什么

- 一台已能运行自制程序的 **NS1**，以及已安装的大气层、Ultrahand 和相应加载器。此仓库不提供系统破解或安装包整合。
- 可联网的 NS，以及电脑读写 SD 卡的方式。
- 自己的百度 **通用文字识别（标准含位置版）** 和 **文本翻译服务** 配置。两个服务分别申请，费用和额度由配置者承担。

已有实机使用记录来自一台 OLED 的此前版本；0.3.0 不承诺覆盖所有 NS1 机型及系统组合。请通过 [反馈模板](docs/试用反馈.md) 记录实际环境。

## 安装与配置

1. 从 [Releases](https://github.com/hilithqiyuanlu/gamelex/releases/tag/v0.3.0) 下载 **`gamelex-0.3.0-trial.zip`**，完整解压到电脑。
2. 关闭 NS，将 SD 卡连接电脑。按包内「先读我.md」使用安装器，或将 `sd/` 内的目录**合并**到卡根目录；不要替换整个 `atmosphere`、`switch` 或 `config` 文件夹。
3. 安装器会在首次安装时创建 `config/gamelex/config.json`。手动安装时，从 [config.example.json](config/config.example.json) 复制一份并命名为 `config.json`；更新时保留已有配置。
4. 填写下表四项。其他字段先保留默认值。
5. 安全弹出卡，插回 NS，**完整重启**进入大气层，再打开英文游戏，从 UH 点击 GameLex。

| 配置字段 | 填写内容 |
| --- | --- |
| `ocr_api_key` | 文字识别应用的 API Key |
| `ocr_secret` | 同一文字识别应用的 Secret Key |
| `translate_appid` | 文本翻译服务的 APP ID，保留字符串引号 |
| `translate_key` | 对应的翻译密钥 |

OCR 需要「标准含位置版」。同时开通「标准版」也可以，但当前分组流程只调用含位置接口，不重复识别。`endpoint`、`api_key` 和 `model` 是早期诊断字段，当前百度流程不用填写。

**仓库和试用包不提供密钥。** 请勿把自己的配置提交到 GitHub，或附在问题反馈里。

### 使用安装器

电脑需 Python 3.9 或以上。在试用包解压目录执行；Windows 假设 SD 为 `E:`，Mac 请替换实际卡名：

```powershell
# Windows：先预览，再安装
python tools/deploy.py --sd E:\
python tools/deploy.py --sd E:\ --apply
```

```sh
# macOS：先预览，再安装
python3 tools/deploy.py --sd "/Volumes/卡名"
python3 tools/deploy.py --sd "/Volumes/卡名" --apply
```

安装器检查哈希、备份覆盖项并保留已有配置。**试用包包含 GameLex 自己的后台自启标记**，不是只复制一个 `.ovl`：

```text
switch/.overlays/gamelex-probe.ovl                 游戏内入口
atmosphere/contents/42000000474C4558/               GameLex 后台及自启标记
config/gamelex/                                   配置、许可证和运行数据
switch/gamelex-device-probe/                      可选设备诊断应用
```

安装、更新和卸载后均需完整重启。浮层和后台必须配套更新；程序会拒绝不匹配的版本。

## 按键

| 所在页面 | 操作 |
| --- | --- |
| 英文分组列表 | ↑↓／左摇杆选择；A 翻译；R 向后多选，L 缩回；+ 重新识别；B 退出 |
| 阅读页 | X 切换中英；Y 进入英文选词；↑↓ 滚动；B 返回分组列表 |
| 原文选词 | 方向键移动；L/R 缩扩相邻词；A 查询；B 返回 |
| 等待或失败 | B 取消／返回；阅读页 + 手动重试翻译，- 查看失败详情 |

触屏支持列表拖动、文字组点选、英文词查询和页面内的操作入口。多选最多八组；选词扩选最多六词，不跨标点或段落。它们是手动范围选择，**不是自动固定搭配识别**。

缓存是有上限的临时会话缓存；退出工具或成功重新识别后，不保证保留。取消等待也不保证服务端停止已经开始的计费请求。

## 当前限制与后续方向

0.3.0 已有识别、分组、按需翻译和原句选词查询，但还不是完整的学习工具：

- 当前查词使用在线翻译，**不是完整离线词典**。
- 尚无生词本、词形还原、自动短语匹配、复习或导出。
- 尚无系统键盘输入、发音和游戏原位置的可点击文字框。
- 自动分组和识别可能出错，尤其是艺术字体、图标与复杂背景。当前不会擅自删除人名、数字或菜单内容。
- UH、加载器、系统更新或其他常驻模块可能影响兼容性，需要重新实测。

后续优先验证核心流程的连续使用、取消恢复和内存共存，再推进本地词典与收藏。不把增加加载器内存或超频作为前提。

## 数据与网络

- 截图发送给配置的百度 OCR 服务；选中的原文发送给百度翻译服务，NS 直接连接服务端。
- HTTPS 证书验证开启，不自动重试收费请求。
- 密钥保存在 SD 卡配置中，不附在发布包，也不写入程序日志。
- `save_last_capture` 默认关闭；开启后会覆盖保存最后一张截图和 OCR 回复用于诊断。即使关闭，最近的运行消息仍包含识别文字。
- 请勿公开整个 `config/gamelex/` 目录。反馈时只提供环境、步骤、错误码，以及自行确认可分享的画面。

## 卸载与回退

在试用包解压目录运行，替换实际卡路径：

```powershell
python tools/uninstall.py --sd E:\
python tools/uninstall.py --sd E:\ --apply
```

Mac 使用 `python3` 与 `/Volumes/卡名` 路径。卸载器先备份，再移出 GameLex 的已知程序文件，保留配置和数据。之后完整重启。

手动卸载可移除上述 GameLex 浮层、后台模块目录和诊断应用，保留 `config/gamelex/`。备份在 `config/gamelex/backups/`；回退时恢复同一版本的浮层与后台，再完整重启。详见 [安装与卸载说明](docs/同学试用.md)。

## 开发与构建

原生 C++，浮层使用 libultrahand／libnx，后台负责截图和网络；位置分组、选词和缓存逻辑可在主机测试。目录和通信边界见 [模块与发布](docs/模块与发布.md)。

仓库保留带项目修改的 libultrahand 源码及原许可，依赖版本和校验值记录在 [deps.lock.json](deps.lock.json)。不包含开发者工具链或私人配置。

已有 devkitPro 环境：

```sh
python3 scripts/fetch_deps.py --sources
python3 scripts/build_switch.py
```

Apple Silicon Mac 可在工程目录内准备固定工具链：

```sh
python3 scripts/fetch_deps.py --mac-sdk
python3 scripts/build_switch.py
```

本机检查与打包：

```sh
python3 scripts/check_host.py
python3 scripts/package.py
python3 scripts/package_source.py
python3 scripts/package_trial.py
```

HTTPS 测试只连接本机临时服务，不调用真实付费接口。当前检查覆盖核心、分组、实际浮层代码的操作替身、协议、安装／卸载、发布及连续上传恢复；**本机通过不等于 NS 实机验收通过**。详细测试范围见 [实机测试说明](docs/实机测试.md)。

## 反馈与贡献

欢迎提交可复现的问题、不同游戏的分组反馈和兼容记录。请阅读 [贡献说明](CONTRIBUTING.md)，并在 [Issues](https://github.com/hilithqiyuanlu/gamelex/issues) 中记录机型、版本、操作步骤和错误码；不要提交密钥、私人配置或整个 SD 卡。

## 许可与致谢

项目自身代码采用 [GPL-2.0-or-later](LICENSE)。第三方文件保留各自许可与作者声明，详见 [THIRD_PARTY.md](THIRD_PARTY.md)。安装包附带相应许可证和对应源码。

感谢 [libultrahand](https://github.com/ppkantorski/libultrahand)、[libnx](https://github.com/switchbrew/libnx)、[nlohmann/json](https://github.com/nlohmann/json)、[libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) 等项目；截图与通信流程参考 [Switch OCR](https://github.com/mariano54/switch-ocr)。GameLex 与 Nintendo、Atmosphère、Ultrahand 及百度没有官方隶属关系。

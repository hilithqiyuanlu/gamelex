# Windows 端交接

优先使用 Mac 读卡。只有读写或构建确需 Windows 时，才转交本工程；无需重做需求或改技术方案。

## 给另一位开发助手的任务

打开同一份工程或解压源码包，先读 `README.md`、`docs/开发状态.md`、`docs/实机测试.md`。当前是第一阶段探测包，后续功能受实机可行性检查约束。

用户设备为日版 OLED，大气层虚拟系统；记录版本 `22.5.0｜AMS1.11.2｜E`。使用 NS 直接联网视觉模型，手柄支持掌机及电视，浮层键盘不可用则延后输入查询。不搭建常驻电脑识别服务，不修改游戏或系统。

## 只负责安装时

无需在 Windows 上重新编译。使用已有 `dist/phase1-sd` 目录，或将测试包解压到独立文件夹。

假设 SD 卡盘符为 `E:`，先检查：

```powershell
python scripts/deploy.py --sd E:\ --package dist/phase1-sd
```

确认目录正确后，复制独立设备诊断包：

```powershell
python scripts/deploy.py --sd E:\ --package dist/phase1-sd --apply
```

需要运行后台探测时增加 `--enable-module`。脚本只按哈希清单复制本工具文件，并备份覆盖项。不得格式化卡、修改分区、替换大气层或浮层加载器；若需要修改已有组件，先报告实际检查结果。

测试结束再关机拔卡，读取 `config/gamelex/reports/`。不要将包含密钥的 `config.json` 发回聊天或加入源码包。报告不能代替玩家观察截图是否正确、游戏是否卡顿。

## 需要重新构建时

建议在 WSL2 或 Docker Desktop 的 Linux 环境构建，避免重新适配 Windows 原生工具链。固定官方镜像：

```text
devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
```

在工程根目录的 Linux 环境执行：

```sh
python3 scripts/fetch_deps.py --sources
python3 scripts/build_switch.py
python3 scripts/package.py
```

若使用 Docker，把工程挂载到 `/work`，工作目录设为 `/work`。不要在工程中保存任何用户密钥；不要把本机 Mac 工具链复制到 Windows 执行。

## 回传内容

只记录：实际固件／大气层版本、现有浮层加载器情况、掌机与电视测试结果、键盘结果、内存和耗时报告、错误码及复现步骤。继续开发前先判断第一阶段是否通过；如果没有通过，保留日志并分析原因。

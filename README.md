# 机场车 A-Box 固件

机场车 A-Box 固件，统一使用 CMake/GCC 构建。公共平台通过 `platform/` Git 子模块固定到具体提交。

公共平台当前能力和本项目待迁移状态见 `platform/README.md`、`platform/docs/PRODUCT_MATRIX.md`。

## 目录

- `app/`、`boot/`：STM32 App 与 BootLoader，包含 CubeMX、HAL/CMSIS、GNU startup、链接脚本和 CMake 工程。
- `platform/`、`platform_config/`：公共平台子模块和产品配置。
- `docs/`：部署和测试资料。
- `tools/`：正式构建工具。
- `dist/`：固件交付目录。

## 正式版构建

```powershell
.\tools\build_release.ps1 -Configuration Release
```

正式产物保持 `ATbox_V1_0.bin`、`ATbox_BootLoader.bin` 和 `release.manifest.json`。平台提交可通过 `git -C platform rev-parse HEAD` 查看。

## 当前 Boot/OTA 状态

本项目尚未迁移到公共 Boot V2。当前 App 只把 OTA 请求写入旧配置页，产品私有 Boot 仍通过 HTTP 下载并直接更新 App，因此 `boot/` 目录暂时保留。公共平台中的 `ABox_Boot.bin` 不能直接替换 `ATbox_BootLoader.bin`，否则现有 OTA 链路会失效。

迁移前必须先在 App 中接入 CA/UFS 下载、稳定槽、Boot State V3 和内部 OTA 能力上报，并确认旧设备是否允许一次性线刷 Full 包。完成真实设备安装、断电恢复、试运行和回滚验收后，才能删除产品私有 Boot。

## RS485 继电器诊断固件

临时 ABox 台架可使用隔离的 `ABOX_RELAY485_TEST` 构建控制 `RELAY_OUT1`。该模式只启用 GPIO、UART5 RXNE 轮询和必要的时基，不启动机场车的 CAN、EC800、MQTT 或业务任务；正式构建默认关闭该选项。

构建诊断镜像：

```powershell
.\tools\build_relay485_test.ps1 -Configuration Release
```

产物位于 `dist\diagnostics\relay485_test\`，其中 `ABox_Relay485_Test_App.bin` 的烧录地址为 `0x08008000`，`ABox_Relay485_Test_Full.bin` 的烧录地址为 `0x08000000`。

安装串口工具依赖：

```powershell
python -m pip install -r .\tools\requirements-relay485.txt
```

常用控制命令（UART5：115200/8N1）：

```powershell
python .\tools\relay485.py --port COM5 status
python .\tools\relay485.py --port COM5 on
python .\tools\relay485.py --port COM5 off
python .\tools\relay485.py --port COM5 cycle 3000
```

固件命令为 `ON`、`OFF`、`STATUS` 和 `CYCLE <off_ms>`，每条命令以 `CRLF` 或 `LF` 结束；成功响应以 `OK` 开头，循环结束发送 `EVENT CYCLE_DONE RELAY_CMD=ON`。`CYCLE` 期间再次发送循环命令返回 `ERR BUSY`，`ON/OFF` 会取消当前循环。

`STATUS` 返回的是继电器 GPIO 命令状态，不是触点或 VCU 电源反馈。台架接线应保持控制 ABox 独立常供电，将 `24V+` 经保险接 `A3/COM`，再从 `A4/NO` 接到 VCU 的 `24V+`；VCU 负极直接回电源负极，RS485 接原理图的 A10/A11。

诊断模式 LED 指示：`COMM` 常亮表示已经进入继电器测试程序，`WORK` 每 500 ms 翻转表示主循环和系统时基正在运行，`LINK` 在 UART5 收到字节后保持亮约 100 ms。若 `COMM/WORK` 正常但发送命令时 `LINK` 不亮，应优先检查 485 A/B、共地、波特率和烧录接线。

CMake 构建成功不能替代 CAN、EC800、Boot 跳转和 OTA 实物验证；固件交付前仍需在实物板上完成冷启动、通信和升级回归。

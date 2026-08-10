# Airport Vehicle A-Box Firmware

机场车 A-Box 固件，迁移期同时保留 Keil 工程和 CMake/GCC 工程。公共平台通过 `platform/` Git 子模块固定到具体提交。

## 目录

- `app/`、`boot/`：STM32 App 与 BootLoader，包含既有 Keil 工程。
- `platform/`、`platform_config/`：公共平台子模块和产品配置。
- `docs/`：部署和测试资料。
- `tools/`：正式构建工具。
- `dist/`：固件交付目录。

## Release 构建

```powershell
.\tools\build_release.ps1 -Configuration Release
```

正式产物保持 `ATbox_V1_0.bin`、`ATbox_BootLoader.bin` 和 `release.manifest.json`。平台提交可通过 `git -C platform rev-parse HEAD` 查看。

CMake 构建成功不能替代 Keil `0 Error(s)`、CAN、EC800、Boot 跳转和 OTA 实物验证。

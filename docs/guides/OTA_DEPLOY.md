# 机场车旧版 OTA 交付说明

> 当前状态：本项目仍使用产品私有 Boot 的 HTTP 下载流程，尚未迁移到公共 Boot V2。本文只描述旧架构交付方式，不适用于公共 `ABox_Boot.bin`。

ATbox BootLoader 的默认下载地址保持为：

```text
http://36.153.162.171:20080/fw/app.bin
```

该文件由独立的 `device-ota-service` 发布。交付时不要把 bin 直接放入
FRP 目录，也不要让设备下载上传中的临时文件。使用 OTA 发布账号将文件上传到
`/var/lib/device-ota-service/incoming/app.bin`，再执行受限的 `publish app.bin
<version> <sha256>` 命令；服务会校验 STM32 App 向量、记录版本和 SHA-256，并原子
更新公开别名。ATbox 当前没有随工程提供的上位机上传程序，因此上传由统一 OTA
发布工具负责。

切换公共 Boot 前，App 必须先具备 EC800 UFS 下载、镜像校验、稳定槽和 Boot State V3 能力。不能只替换 Boot 二进制，也不能仅凭服务器发布成功认定设备升级成功。

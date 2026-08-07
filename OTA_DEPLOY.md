# ATbox OTA 交付说明

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


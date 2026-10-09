# 机场车 A-Box 工作区约束

## 目录

- `app/`：机场车 App 工程。
- `boot/`：尚未迁移的机场车旧 Boot 工程；当前仍负责 HTTP 下载，不能直接替换为公共 `ABox_Boot.bin`。
- 根目录只放仓库级文件，例如 `.git`、`AGENTS.md`、README 和构建工具。
- App 与旧 Boot 只支持 CMake + ARM GNU 工具链。

## 构建入口

- App：`app/CMakeLists.txt`、`app/CMakePresets.json`。
- 旧 Boot：`boot/CMakeLists.txt`、`boot/CMakePresets.json`。
- 正式发布：`tools/build_release.ps1`。

## Flash 布局

- Boot：`0x08000000..0x08007FFF`。
- App 起始地址：`0x08008000`。
- 旧 OTA 信息页：`0x0803F800`。

## 工作规则

- 未经明确要求不得回退用户未提交的改动。
- 除非来自有意重建或用户明确要求，不得手工修改生成的构建产物。
- 修改 Flash 地址时，必须同步 App 与旧 Boot 的 OTA 布局常量。
- GNU 链接脚本和 CMake 源文件列表必须与 CubeMX 生成代码保持一致。
- 不得恢复旧 IDE 工程或工具链专用启动文件。
- 本项目尚未完成 Boot V2 App 下载器、稳定槽和 Boot State V3 迁移；迁移完成并经过真实设备验收前，不得删除 `boot/`，也不得声称公共 Boot 已可直接使用。

# FireflyOS Arduino IDE 配置指南

> 适用硬件：Waveshare ESP32-S3-Touch-AMOLED-2.06  
> Arduino 开发板：ESP32S3 Dev Module  
> FireflyOS 图形库基线：LVGL 8.3.11  
> 更新日期：2026-07-01

## 1. 配置前须知

### 1.1 Sketchbook 不是当前 `.ino` 所在目录

Arduino IDE 的 Sketchbook（项目文件夹位置）是工作区根目录。IDE 会从以下位置查找用户库：

```text
<Sketchbook>/libraries/
```

FireflyOS 的主工作区结构为：

```text
FireflyOS/
├─ Firefly/
│  └─ Firefly.ino
└─ libraries/
   ├─ lvgl/
   ├─ FireflyOS/
   ├─ Arduino_GFX/
   ├─ SensorLib/
   └─ ...
```

因此，编译不同分支时应使用对应工作树的根目录：

| 编译目标 | Sketchbook 路径 | 应打开的程序 |
|---|---|---|
| `main`，计划 1 基线 | `D:\Study\Projects\ESP32Projects\FireflyOS` | `D:\Study\Projects\ESP32Projects\FireflyOS\Firefly\Firefly.ino` |
| `codex/ui-shell`，计划 2 | `D:\Study\Projects\ESP32Projects\FireflyOS\.worktrees\ui-shell` | `D:\Study\Projects\ESP32Projects\FireflyOS\.worktrees\ui-shell\Firefly\Firefly.ino` |

当前计划 2 尚未合并到 `main`。执行计划 2 的编译或真机 Gate B 时，必须使用 `ui-shell` 工作树。

不要把 Sketchbook 设置为以下目录：

- `...\FireflyOS\Firefly`：IDE 会错误地查找 `Firefly\libraries`。
- `...\FireflyOS\.worktrees`：该目录下没有直接对应的 `libraries`。
- `...\FireflyOS\libraries`：这是库目录本身，不是 Sketchbook 根目录。

修改 Sketchbook 后应完全重启 Arduino IDE，再重新打开对应工作树中的 `Firefly.ino`。

### 1.2 ESP32 开发板包版本

微雪当前官方资料要求：

```text
esp32 by Espressif Systems >= 3.2.0
```

本机检查时 Arduino CLI 识别到的是 `2.0.17`。该版本低于微雪当前要求，并可能在 32MB 分区、USB 菜单或构建流程上表现不同。建议在 Arduino IDE 的开发板管理器中升级到受支持版本，然后重新执行完整编译验证。

升级开发板包属于构建环境变化。升级后必须重新编译核心测试固件和 Firefly 主固件，不能直接沿用旧环境的编译结论。

### 1.3 LVGL 版本不能随官方示例升级

FireflyOS 固定使用仓库内的 LVGL 8.3.11。即使微雪当前示例使用 LVGL 9，也不要通过 Arduino Library Manager 将 FireflyOS 的 LVGL 覆盖为 v9。

必须同时保持：

- ESP32 Arduino 开发板包使用适合该硬件的版本。
- FireflyOS 图形库继续使用项目仓库中的 LVGL 8.3.11。
- `lv_conf.h` 使用当前工作树 `libraries/lv_conf.h`。
- 不在 FireflyOS 源码中混用 LVGL 9 API。

## 2. Arduino IDE 工具菜单推荐设置

以下设置用于当前 FireflyOS 开发和计划 2 真机验证。

| 工具菜单项 | 推荐值 | 说明 |
|---|---|---|
| Board | `ESP32S3 Dev Module` | 对应 ESP32-S3R8 |
| USB Mode | `Hardware CDC and JTAG` | 使用 ESP32-S3 原生 USB Serial/JTAG 控制器 |
| USB CDC On Boot | `Enabled` | 让项目中的 `Serial.begin(115200)` 通过 Type-C 输出日志 |
| USB Firmware MSC On Boot | `Disabled` | 当前不把设备模拟成 USB 存储盘 |
| USB DFU On Boot | `Disabled` | 当前开发流程使用 Hardware CDC/esptool，不使用 DFU 常驻启动 |
| Upload Mode | `UART0 / Hardware CDC` | ESP32-S3 原生 USB CDC 的推荐上传模式 |
| CPU Frequency | `240MHz (WiFi)` | 使用 ESP32-S3 的最高额定主频 |
| Flash Mode | `QIO 80MHz` | 项目当前采用的高速稳定模式 |
| Flash Size | `32MB (256Mb)` | 与板载 32MB Flash 一致 |
| Partition Scheme | `32M Flash (4.8MB APP/22MB FATFS)` | 提供双应用槽、FATFS 和当前固件所需空间 |
| PSRAM | `OPI PSRAM` | 启用板载 8MB OPI PSRAM |
| Arduino Runs On | `Core 0` | 与 FireflyOS 构建配置一致，UI/LVGL 主循环运行于该核心 |
| Events Run On | `Core 0` | 与当前软件验证配置一致 |
| Core Debug Level | `None` | 正常运行减少框架日志；排错时可临时改为 `Info` |
| Upload Speed | `921600` | 稳定时使用；上传失败可降为 `460800` 或 `115200` |
| Erase All Flash Before Sketch Upload | `Disabled` | 日常上传保留 NVS 和用户设置 |
| JTAG Adapter | `Disabled` | 只有进行断点调试时才启用 `Integrated USB JTAG` |
| Programmer | `Esptool` | 如果菜单中显示该项，保持默认即可 |

## 3. 设置原则

### 3.1 USB CDC、DFU 和 MSC 不是性能开关

这三项承担不同功能：

- CDC：提供串口通信、日志和常规开发上传能力。
- DFU：通过 USB Device Firmware Upgrade 模式烧录固件。
- MSC：把设备模拟成 USB 大容量存储设备。

FireflyOS 当前只需要 CDC。开启 DFU 或 MSC 不会提高 CPU、屏幕或 LVGL 性能，反而会改变 USB 枚举方式并增加调试复杂度。

该开发板的 Type-C 烧录和调试口直接连接 ESP32-S3 原生 USB。FireflyOS 使用 `Serial` 输出日志，因此应启用 `USB CDC On Boot`。

### 3.2 不使用 QIO 120MHz 追求表面性能

当前项目采用 `QIO 80MHz`。在没有完成独立真机稳定性、冷启动和长时间运行验证前，不切换到 `QIO 120MHz`。

Flash 频率提高不等于 UI 帧率必然提高。FireflyOS 的显示性能还受 QSPI 屏幕刷新、LVGL 绘制、内存带宽和图片资源格式影响。

### 3.3 双核设置必须服从 LVGL 单核边界

当前构建配置将 Arduino `setup()`、`loop()` 和 LVGL 主循环放在 Core 0。FireflyOS 的轻量后台任务放到另一核心，只负责按键、息屏时序等非 UI 工作，并向有界事件总线投递事件。

无论核心菜单如何设置，都必须保持：

- LVGL 只由 UI 主循环访问。
- 后台任务不得调用 `lv_obj_*`、`lv_label_*`、`lv_img_*` 等 API。
- 后台任务只更新状态或投递事件。
- RTC、PMU、IMU 和 Codec 的共享 I2C 访问必须经过互斥边界。

不要仅为了“让两个核心都忙起来”而随意拆分 UI。对当前项目而言，稳定的单核 UI 所有权比平均分摊负载更重要。

### 3.4 Flash 擦除策略

正常上传时保持 `Erase All Flash Before Sketch Upload = Disabled`。

以下情况可以临时启用一次完整擦除：

- 第一次从错误的 4MB 分区切换为 32MB 分区。
- 分区方案发生变化。
- NVS 数据结构不兼容或设置数据已损坏。
- 固件反复重启，且已排除普通代码问题。

完整擦除会清除 NVS、闹钟、设置、配对信息和 Flash 文件系统数据。擦除并成功上传一次后，应把该选项恢复为 `Disabled`。

## 4. 首次配置与上传步骤

1. 在 Arduino IDE 的开发板管理器中检查 `esp32 by Espressif Systems` 版本。
2. 将 Sketchbook 设置为当前需要编译的工作树根目录。
3. 完全关闭并重新启动 Arduino IDE。
4. 打开对应工作树中的 `Firefly/Firefly.ino`。
5. 选择 `ESP32S3 Dev Module` 和正确的 USB COM 端口。
6. 按第 2 章设置所有工具菜单项。
7. 先执行“验证/编译”，确认使用的是当前工作树中的库。
8. 再执行上传。
9. 首次启用 USB CDC 后，如果端口发生变化，重新选择新出现的 COM 端口。
10. 打开串口监视器，波特率选择 `115200`。

如果自动上传失败，可关闭串口监视器，按住 BOOT 后重新上电进入下载模式，再降低 Upload Speed 重试。

## 5. 常见问题

### 5.1 `fatal error: lvgl.h: No such file or directory`

原因通常是 Sketchbook 指向错误，Arduino IDE 没有扫描当前工作树的 `libraries`。

检查：

- Sketchbook 是否为工作树根目录，而不是 `Firefly` 或 `libraries`。
- 是否在修改 Sketchbook 后重启 Arduino IDE。
- 是否打开了目标工作树中的 `Firefly.ino`。
- `libraries/lvgl/library.properties` 中的版本是否为 `8.3.11`。

不要通过把 `#include <lvgl.h>` 改成相对路径来绕过库配置问题。

### 5.2 编译的是旧版 main，而不是计划 2

如果编译错误路径以以下内容开头：

```text
D:\Study\Projects\ESP32Projects\FireflyOS\Firefly\
```

表示当前编译的是 `main` 工作区。

计划 2 的路径应包含：

```text
D:\Study\Projects\ESP32Projects\FireflyOS\.worktrees\ui-shell\Firefly\
```

### 5.3 串口监视器没有输出

依次确认：

1. `USB Mode = Hardware CDC and JTAG`。
2. `USB CDC On Boot = Enabled`。
3. 串口监视器选择了重启后新出现的端口。
4. 波特率为 `115200`。
5. 数据线支持 USB 数据传输，而不仅是充电。

### 5.4 上传失败或端口消失

- 关闭串口监视器后重试。
- 把 Upload Speed 降至 `460800` 或 `115200`。
- 完全断电后重新连接。
- 必要时按住 BOOT，再上电进入强制下载模式。
- 检查 USB 线和 USB 端口。

### 5.5 32MB 分区文件缺失

如果出现 `app5M_fat24M_32MB`、`large_fat_32MB.csv` 或类似分区文件缺失提示，优先检查：

- ESP32 开发板包是否低于微雪要求的版本。
- Flash Size 是否为 32MB。
- Partition Scheme 是否选择了 32MB FATFS 方案。
- Arduino IDE 是否仍缓存旧开发板包配置。

更新开发板包或分区设置后，应重新启动 Arduino IDE并执行干净编译。

## 6. 编译与真机验证检查表

- [ ] 当前打开的 `.ino` 位于目标工作树。
- [ ] Sketchbook 指向该工作树根目录。
- [ ] 编译使用当前工作树内的 `libraries/lvgl`。
- [ ] LVGL 版本为 8.3.11。
- [ ] Flash Size 为 32MB。
- [ ] Flash Mode 为 QIO 80MHz。
- [ ] PSRAM 为 OPI PSRAM。
- [ ] CPU 为 240MHz。
- [ ] USB CDC On Boot 已启用。
- [ ] 主固件完成一次干净编译。
- [ ] 上传后串口日志可以正常读取。
- [ ] 启动日志中的 Flash、PSRAM 和核心配置与预期一致。

工具菜单设置正确只代表构建配置正确，不能代替触摸、圆角裁切、内存恢复、转场阻塞、息屏轮播和覆盖层优先级等 Gate B 真机测试。

## 7. 官方参考资料

- [Waveshare ESP32-S3-Touch-AMOLED-2.06 Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06)
- [Espressif Arduino-ESP32 USB CDC 与 DFU 说明](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/cdc_dfu_flash.html)


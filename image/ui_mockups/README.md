# FireflyOS UI 全量预览

本目录包含 **66 张手表端界面**、**18 张 Android 伴侣端界面**，以及联系表、圆角安全区标注图和可浏览图库。

## 设计基线

- 手表原生画布：410 × 502 px，RGBA PNG；物理屏圆角预设半径 44 px。
- Android 伴侣端：432 × 960 px 设计预览，用于建立信息架构与视觉一致性，不代替最终 Android 适配稿。
- 日常态：深黑 AMOLED 背景、萤火青绿、浅青与少量梦境淡紫。
- 高优先级态：SAM 机械线条、能量黄绿、点火橙与危险红。
- 关键内容避开四角裁切；按钮与主要触控项按不小于 48 × 48 px 设计。
- 当前使用抽象能量翼和几何图标占位，后续可按总纲中的 AI 提示词替换为正式资产。

## 使用方式

- 打开 `index.html` 可逐张浏览。
- `watch/` 与 `android/` 保存单张原图。
- `guides/watch-safe-area.png` 用于真机校准屏幕圆角与内容安全区。
- 修改 `tools/render_ui_mockups.py` 中的视觉令牌或 `DISPLAY_RADIUS`，重新运行即可全量生成。

## 界面清单

| 编号 | 平台 | 分类 | 界面 | 文件 |
|---|---|---|---|---|
| W01_boot | watch | 系统与外壳 | 启动画面 | `watch/W01_boot.png` |
| W02_glance | watch | 系统与外壳 | 一瞥息屏 | `watch/W02_glance.png` |
| W03_lock | watch | 系统与外壳 | 锁屏 | `watch/W03_lock.png` |
| W04_home_1 | watch | 系统与外壳 | 应用主页第1页 | `watch/W04_home_1.png` |
| W05_home_2 | watch | 系统与外壳 | 应用主页第2页 | `watch/W05_home_2.png` |
| W06_control_center | watch | 系统与外壳 | 控制中心 | `watch/W06_control_center.png` |
| W07_notifications | watch | 系统与外壳 | 通知中心 | `watch/W07_notifications.png` |
| W08_notifications_empty | watch | 系统与外壳 | 通知中心空状态 | `watch/W08_notifications_empty.png` |
| W09_notification_detail | watch | 系统与外壳 | 通知详情 | `watch/W09_notification_detail.png` |
| W10_power_menu | watch | 系统与外壳 | 电源菜单 | `watch/W10_power_menu.png` |
| W11_charging | watch | 系统与外壳 | 充电覆盖层 | `watch/W11_charging.png` |
| W12_alarm_ringing | watch | SAM高优先级 | 闹钟响铃 | `watch/W12_alarm_ringing.png` |
| W13_critical_battery | watch | SAM高优先级 | 严重低电量 | `watch/W13_critical_battery.png` |
| W14_pairing_confirm | watch | 连接与权限 | 手表配对确认 | `watch/W14_pairing_confirm.png` |
| W15_pairing_result | watch | 连接与权限 | 配对成功 | `watch/W15_pairing_result.png` |
| W16_hardware_degraded | watch | 系统与外壳 | 硬件降级提示 | `watch/W16_hardware_degraded.png` |
| W17_permission_prompt | watch | 连接与权限 | 麦克风权限 | `watch/W17_permission_prompt.png` |
| W18_clock_hub | watch | 时钟与日程 | 时钟中心 | `watch/W18_clock_hub.png` |
| W19_alarm_list | watch | 时钟与日程 | 闹钟列表 | `watch/W19_alarm_list.png` |
| W20_alarm_editor | watch | 时钟与日程 | 闹钟编辑 | `watch/W20_alarm_editor.png` |
| W21_alarm_keyboard | watch | 时钟与日程 | 闹钟名称键盘 | `watch/W21_alarm_keyboard.png` |
| W22_timer_idle | watch | 时钟与日程 | 计时器待机 | `watch/W22_timer_idle.png` |
| W23_timer_running | watch | 时钟与日程 | 计时器运行 | `watch/W23_timer_running.png` |
| W24_stopwatch | watch | 时钟与日程 | 秒表 | `watch/W24_stopwatch.png` |
| W25_calendar_month | watch | 时钟与日程 | 日历月视图 | `watch/W25_calendar_month.png` |
| W26_calendar_agenda | watch | 时钟与日程 | 日历日程视图 | `watch/W26_calendar_agenda.png` |
| W27_settings_main | watch | 设置 | 设置主页 | `watch/W27_settings_main.png` |
| W28_settings_sound | watch | 设置 | 声音设置 | `watch/W28_settings_sound.png` |
| W29_settings_time | watch | 设置 | 时间设置 | `watch/W29_settings_time.png` |
| W30_settings_battery | watch | 设置 | 电池设置 | `watch/W30_settings_battery.png` |
| W31_settings_display | watch | 设置 | 显示设置 | `watch/W31_settings_display.png` |
| W32_settings_connectivity | watch | 设置 | 连接设置 | `watch/W32_settings_connectivity.png` |
| W33_settings_notifications | watch | 设置 | 通知与隐私设置 | `watch/W33_settings_notifications.png` |
| W34_settings_themes | watch | 设置 | 主题设置 | `watch/W34_settings_themes.png` |
| W35_device_info | watch | 设置 | 设备信息 | `watch/W35_device_info.png` |
| W36_diagnostics | watch | 设置 | 系统诊断 | `watch/W36_diagnostics.png` |
| W37_activity_today | watch | 活动与工具 | 今日活动 | `watch/W37_activity_today.png` |
| W38_activity_details | watch | 活动与工具 | 活动详情 | `watch/W38_activity_details.png` |
| W39_motion_unavailable | watch | 活动与工具 | 运动传感器不可用 | `watch/W39_motion_unavailable.png` |
| W40_calculator | watch | 活动与工具 | 计算器 | `watch/W40_calculator.png` |
| W41_flashlight | watch | 活动与工具 | 屏幕手电筒 | `watch/W41_flashlight.png` |
| W42_files_root | watch | 存储与媒体 | 文件根目录 | `watch/W42_files_root.png` |
| W43_files_list | watch | 存储与媒体 | 文件列表 | `watch/W43_files_list.png` |
| W44_sd_unavailable | watch | 存储与媒体 | SD 卡不可用 | `watch/W44_sd_unavailable.png` |
| W45_music_library | watch | 存储与媒体 | 音乐库 | `watch/W45_music_library.png` |
| W46_music_now_playing | watch | 存储与媒体 | 正在播放 | `watch/W46_music_now_playing.png` |
| W47_music_empty | watch | 存储与媒体 | 音乐空状态 | `watch/W47_music_empty.png` |
| W48_recorder_idle | watch | 存储与媒体 | 录音待机 | `watch/W48_recorder_idle.png` |
| W49_recorder_recording | watch | 存储与媒体 | 正在录音 | `watch/W49_recorder_recording.png` |
| W50_recordings_list | watch | 存储与媒体 | 录音列表 | `watch/W50_recordings_list.png` |
| W51_themes_gallery | watch | 主题 | 主题图库 | `watch/W51_themes_gallery.png` |
| W52_theme_detail | watch | 主题 | 主题详情 | `watch/W52_theme_detail.png` |
| W53_theme_import_error | watch | 主题 | 主题导入失败 | `watch/W53_theme_import_error.png` |
| W54_storage_info | watch | 存储与媒体 | 存储详情 | `watch/W54_storage_info.png` |
| W55_weather_fresh | watch | 网络服务 | 天气正常 | `watch/W55_weather_fresh.png` |
| W56_weather_stale | watch | 网络服务 | 天气缓存过期 | `watch/W56_weather_stale.png` |
| W57_weather_no_location | watch | 网络服务 | 天气无城市 | `watch/W57_weather_no_location.png` |
| W58_wifi_provision | watch | 网络服务 | Wi-Fi 配网 | `watch/W58_wifi_provision.png` |
| W59_transfer_receiving | watch | 连接与传输 | 文件接收 | `watch/W59_transfer_receiving.png` |
| W60_update_available | watch | 系统更新 | 发现更新 | `watch/W60_update_available.png` |
| W61_update_download | watch | 系统更新 | 下载更新 | `watch/W61_update_download.png` |
| W62_update_installing | watch | 系统更新 | 安装更新 | `watch/W62_update_installing.png` |
| W63_update_rollback | watch | 系统更新 | 更新回滚 | `watch/W63_update_rollback.png` |
| W64_find_watch | watch | SAM高优先级 | 查找手表 | `watch/W64_find_watch.png` |
| W65_factory_reset | watch | 系统与外壳 | 恢复出厂确认 | `watch/W65_factory_reset.png` |
| W66_low_memory | watch | SAM高优先级 | 低内存保护 | `watch/W66_low_memory.png` |
| A01_welcome | android | 首次连接 | 欢迎与未连接 | `android/A01_welcome.png` |
| A02_device_scan | android | 首次连接 | 扫描设备 | `android/A02_device_scan.png` |
| A03_pairing_code | android | 首次连接 | 配对码确认 | `android/A03_pairing_code.png` |
| A04_dashboard_connected | android | 设备 | 已连接仪表盘 | `android/A04_dashboard_connected.png` |
| A05_dashboard_offline | android | 设备 | 离线仪表盘 | `android/A05_dashboard_offline.png` |
| A06_notification_permission | android | 通知 | 通知权限引导 | `android/A06_notification_permission.png` |
| A07_notification_filter | android | 通知 | 通知应用筛选 | `android/A07_notification_filter.png` |
| A08_settings_sync | android | 设置 | 设备同步设置 | `android/A08_settings_sync.png` |
| A09_theme_manager | android | 主题 | 主题管理 | `android/A09_theme_manager.png` |
| A10_weather_city | android | 天气 | 天气城市 | `android/A10_weather_city.png` |
| A11_calendar_permission | android | 日历 | 日历同步权限 | `android/A11_calendar_permission.png` |
| A12_media_remote | android | 媒体 | 媒体遥控 | `android/A12_media_remote.png` |
| A13_find_device | android | 设备 | 查找设备 | `android/A13_find_device.png` |
| A14_wifi_provision | android | 网络 | Wi-Fi 配网 | `android/A14_wifi_provision.png` |
| A15_transfer_manager | android | 传输 | 传输管理 | `android/A15_transfer_manager.png` |
| A16_update_available | android | 更新 | 固件更新可用 | `android/A16_update_available.png` |
| A17_update_progress | android | 更新 | 固件更新传输 | `android/A17_update_progress.png` |
| A18_unbind_confirm | android | 设备 | 解除绑定确认 | `android/A18_unbind_confirm.png` |

## 实机校准注意

当前 44 px 圆角半径是基于 410 × 502 竖向圆角屏的保守预设。正式进入固件前，应使用纯色边框测试图在真机上测量可见像素边界，再修改半径与顶部/底部安全间距。

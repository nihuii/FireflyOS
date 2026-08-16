# UI Shell 与导航覆盖层

## 来源

- 固定提交：`e4fb0d1ff71bcc0330b507fa90c653be9929e611`
- 主要路径：`libraries/FireflyOS/src/firefly/ui/`、`Firefly/Firefly.ino`、`Firefly/FireflyInteraction.cpp`
- 取回示例：`git show "e4fb0d1:libraries/FireflyOS/src/firefly/ui/UiShell.h"`

## 复用等级

**需要重构后复用。** 四层 Shell、固定深度导航、覆盖层优先级和快照刷新值得保留；下拉系统面板的事件绑定是已确认缺陷，只能作为反例或诊断参考。

## 模块定位

UI Shell 常驻并统一拥有应用层、状态栏、系统面板和系统覆盖层。普通应用页面应按需创建和释放，后台服务只能向 UI 主循环提交数据或命令。

## 职责与边界

- `UiShell`：创建四个 LVGL 层，路由页面，切换控制/通知页，刷新系统快照。
- `NavigationController`：固定深度 6 的路由栈；锁屏时清空回退历史。
- `SystemOverlayHost`：按 1～5 优先级管理覆盖层，闹钟优先级高于配对。
- `ControlCenter` / `NotificationCenter`：只消费状态和回调，不直接访问 PMU、BLE 或 Wi-Fi。
- `FireflyInteraction.cpp`：承载旧版全局手势和大量装配逻辑，是重启时需要继续拆小的文件。

所有 `lv_obj_*`、动画、样式和页面创建必须只在 UI 主循环执行。后台任务不得持有 LVGL 对象指针。

## 数据流与线程边界

```text
服务快照 / SystemEvent
        │
        ▼
UI 主循环（唯一 LVGL 所有者）
        │
        ├─ UiShell.refresh(snapshot, revision)
        ├─ NavigationController.open/back
        └─ SystemOverlayHost.show/close
```

410×502 圆角屏继续采用至少 24px 安全边距和至少 48px 主要触控目标；覆盖层只能通过 Shell 仲裁，不能由应用直接把对象提到最前。

## 关键接口

| 类型 | 约束 |
|---|---|
| `UiShell` | 应用、状态栏、面板、覆盖层四个固定宿主 |
| `NavigationController` | `kDepth = 6`，不动态增长 |
| `SystemOverlayHost` | 优先级范围 1～5；配对 3，闹钟 4 |
| `UiTokens` / `UiComponents` | 集中颜色、间距、圆角和控件样式 |

## 精选代码

来源：`libraries/FireflyOS/src/firefly/ui/UiShell.h`，符号 `UiShell`。

```cpp
bool create(lv_obj_t * screen, const UiTokens & tokens);
bool showRoute(Route route);
Route back();
void bindPanelPages(lv_obj_t * control_page, lv_obj_t * notification_page);
bool showOverlay(uint8_t priority, lv_obj_t * overlay);
void refresh(const SystemState & state, uint32_t revision);

lv_obj_t * appHost() const { return app_host_; }
lv_obj_t * statusBarHost() const { return status_bar_; }
lv_obj_t * panelHost() const { return panel_host_; }
lv_obj_t * overlayHost() const { return overlay_host_; }
```

来源：`libraries/FireflyOS/src/firefly/ui/NavigationController.h`，符号 `NavigationController`。

```cpp
class NavigationController {
public:
    static constexpr uint8_t kDepth = 6;
    bool open(Route route);
    Route back();
    Route current() const;
    void lock();

private:
    Route stack_[kDepth]{};
    uint8_t depth_ = 1;
};
```

来源：`libraries/FireflyOS/src/firefly/ui/screens/SystemOverlayHost.h`，符号 `acceptsPriority`。

```cpp
static bool acceptsPriority(uint8_t current, uint8_t incoming) {
    return incoming >= 1 && incoming <= 5 && incoming >= current;
}
```

来源：`Firefly/Firefly.ino`，系统面板事件绑定。下面是需要避免照搬的反例：

```cpp
lv_obj_add_event_cb(notif_panel, status_drag_cb, LV_EVENT_ALL, NULL);
control_center.create(notif_panel, ui_tokens);

lv_obj_t * notif_handle = lv_obj_create(notif_panel);
lv_obj_set_size(notif_handle, 84, 6);
lv_obj_add_event_cb(notif_handle, status_drag_cb, LV_EVENT_ALL, NULL);
```

全屏 `ControlCenter` 和 `NotificationCenter` 根对象位于 `notif_panel` 内部，但没有统一把按压事件交给父对象；在 LVGL 8.3.11 中，子对象会成为命中目标，大部分向上滑动不会到达 `status_drag_cb`。

## 源码与测试映射

- Shell：`ui/UiShell.*`、`UiTokens.h`、`UiTheme.*`、`UiComponents.*`。
- 导航：`ui/NavigationController.*`、`Screen.h`。
- 页面：`ui/screens/AppShellScreen.*`、`ControlCenter.*`、`NotificationCenter.*`、`HomeScreen.*`、`LockScreen.*`、`GlanceScreen.*`。
- 覆盖层：`ui/screens/SystemOverlayHost.*`。
- 装配与手势：`Firefly/Firefly.ino`、`Firefly/FireflyInteraction.cpp`。
- 测试：`tests/python/test_repository_contracts.py`；真机清单：`docs/UI预览/01-Shell/Gate-B验收记录.md`。

## 验证边界

合约测试能确认路由、层级、触控尺寸常量和页面同级关系；不能模拟真实触控命中、圆角边缘或连续滑动。Gate B 真机项目仍为 `PENDING`。

## 已知问题

- 下拉面板无法通过大部分区域稳定上滑返回，是当前 `ui-shell` 已知缺陷。
- `FireflyInteraction.cpp` 同时承担手势、服务命令、状态转场和多个应用回调，重启时不应继续扩张。
- 面板动画使用固定高度 502；若以后引入旋转或不同屏幕，必须由屏幕配置提供尺寸。
- 覆盖层优先级能仲裁显示顺序，但不能替代业务资源互斥。

## 基于 main 的复用步骤

1. 先保留四层 Shell、固定导航深度和覆盖层优先级，不接计划 3 之后的服务。
2. 为面板建立覆盖整个可交互区域的单一手势捕获层，或明确给子页面启用事件冒泡。
3. 在桌面下拉、面板上滑、控制/通知切换上建立可重复的事件测试和真机 20 次手势记录。
4. 通过 Gate B 后再增加应用路由；每次只增加一个页面和一条返回路径。
5. 所有 UI 更新继续由主循环消费快照，禁止后台线程调用 LVGL。


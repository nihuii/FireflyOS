# UI Shell 与导航覆盖层

## 目标

当前 main 已包含四层 UI Shell、固定深度导航、锁屏/桌面、控制中心、通知中心和系统覆盖层。首要任务不是增加页面，而是修复系统面板无法在大部分区域上滑返回的问题。

## 推荐边界

- LVGL 对象、动画、样式和页面生命周期只属于 UI 主循环。
- Shell 固定拥有应用层、状态栏、系统面板和最高优先级覆盖层。
- 应用提交命令并读取快照，不直接访问 PMU、BLE、Wi-Fi 或文件系统。
- 导航栈固定容量；锁屏时清空回退历史。
- 面板手势由覆盖完整交互区域的捕获层统一处理，或让全部子对象明确冒泡；不能只绑定父面板和窄把手。
- 适配 410×502 圆角安全区，主要触摸目标至少 48px。

## 最小实现

1. 保留现有 Shell 层级和最小锁屏/桌面路由。
2. 统一面板按压、拖动、释放和取消事件的所有权。
3. 验证桌面下拉、面板上滑、控制/通知切换各 20 次。
4. 再加入一个应用路由和一条返回路径。
5. 每次视觉变更先更新预览并记录真机复核。

## 主要风险

当前全屏子页面会成为触控命中对象，使父面板的 `status_drag_cb` 收不到大部分上滑事件。`FireflyInteraction.cpp` 同时承担手势、状态转换和业务回调，继续扩张会使问题难以归因。自动合约不能代替真实触摸、圆角边缘和连续动画验证。

## 参考路径

- 当前：`libraries/FireflyOS/src/firefly/ui/UiShell.*`
- 当前：`libraries/FireflyOS/src/firefly/ui/NavigationController.*`
- 当前：`libraries/FireflyOS/src/firefly/ui/screens/`
- 当前：`Firefly/FireflyInteraction.cpp`
- 当前：`docs/UI预览/01-Shell/index.html`
- 当前：`tests/python/test_repository_contracts.py`

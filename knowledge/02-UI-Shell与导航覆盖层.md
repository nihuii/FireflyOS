# UI Shell 与导航覆盖层

## 目标

当前 main 已包含四层 UI Shell、固定深度导航、锁屏/桌面、控制中心、通知中心和系统覆盖层。系统面板源码已经加入完整后代事件冒泡、16px 方向仲裁和父面板统一关闭回调；首要任务不是增加页面，而是在目标板关闭该修复及 BOOT/息屏的真机基线。

## 推荐边界

- LVGL 对象、动画、样式和页面生命周期只属于 UI 主循环。
- Shell 固定拥有应用层、状态栏、系统面板和最高优先级覆盖层。
- 应用提交命令并读取快照，不直接访问 PMU、BLE、Wi-Fi 或文件系统。
- 导航栈固定容量；锁屏时清空回退历史。
- 面板全部后代显式冒泡到父面板；父面板是上滑关闭的唯一所有者，顶部状态栏只负责下拉打开。
- 纵向上滑接管后阻止原按钮点击，并恢复滑杆门槛形成前可能改变的模型、持久化值与亮度硬件状态。
- 适配 410×502 圆角安全区，主要触摸目标至少 48px。

## 最小实现

1. 保留现有 Shell 层级和最小锁屏/桌面路由。
2. 维持当前完整冒泡链、纯方向仲裁器和 pressed/pressing/released/press-lost 收尾。
3. 在目标板验证桌面下拉、面板上滑、控制/通知切换各 20 次，并覆盖按钮与滑杆起手。
4. 再加入一个应用路由和一条返回路径。
5. 每次视觉变更先更新预览并记录真机复核。

## 主要风险

事件截断的源码原因已经由完整冒泡链处理，但目标板尚未完成各命中对象 20 次矩阵，不能把自动契约和编译写成真机稳定。`FireflyInteraction.cpp` 仍同时承担手势、状态转换和业务回调，继续扩张会使问题难以归因；后续动态创建面板子对象时也必须同步启用冒泡。

## 参考路径

- 当前：`libraries/FireflyOS/src/firefly/ui/UiShell.*`
- 当前：`libraries/FireflyOS/src/firefly/ui/NavigationController.*`
- 当前：`libraries/FireflyOS/src/firefly/ui/PanelGestureArbiter.*`
- 当前：`libraries/FireflyOS/src/firefly/ui/screens/`
- 当前：`Firefly/FireflyInteraction.cpp`
- 当前：`docs/UI预览/01-Shell/index.html`
- 当前：`tests/python/test_repository_contracts.py`

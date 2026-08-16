# System Panel Swipe Dismiss Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让系统面板从背景、卡片、按钮、滑杆和通知项起手的纵向上滑都能可靠返回桌面，同时保留按钮轻点与滑杆横向调节。

**Architecture:** 用不依赖 LVGL 的 `PanelGestureArbiter` 固化 16px 方向判定，并让系统面板的全部后代把指针事件冒泡到父面板。下拉打开与上滑关闭使用独立回调；关闭接管时阻止原控件点击，并恢复门槛形成前可能改变的音量持久化值与亮度硬件状态。

**Tech Stack:** Arduino C++、LVGL 8.3.11、ESP32 Arduino、Python 3 `unittest`、PowerShell。

**Delivery constraint:** 当前唯一基线为 `main`，遵照交接在当前工作区执行；本计划不执行 `git add`、`git commit`、`git push`、合并或打 tag。

---

### Task 1: 用核心测试定义方向仲裁器

**Files:**
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`
- Create: `libraries/FireflyOS/src/firefly/ui/PanelGestureArbiter.h`
- Create: `libraries/FireflyOS/src/firefly/ui/PanelGestureArbiter.cpp`
- Modify: `libraries/FireflyOS/src/FireflyOS.h`

- [x] **Step 1: 写失败的纯逻辑测试**

在核心测试中增加 `test_panel_gesture_arbiter()`，覆盖未达门槛、上滑纵向占优、横向占优、下滑、决策锁定、未开始更新和复位：

```cpp
static void test_panel_gesture_arbiter() {
    firefly::PanelGestureArbiter arbiter;
    expect_true(arbiter.update(0, -20) == firefly::PanelGestureDecision::Ignore,
                "inactive panel gesture is ignored");
    arbiter.begin(100, 100);
    expect_true(arbiter.update(100, 85) == firefly::PanelGestureDecision::Pending,
                "panel gesture waits below threshold");
    expect_true(arbiter.update(102, 80) == firefly::PanelGestureDecision::Dismiss,
                "vertical upward panel gesture dismisses");
    expect_true(arbiter.update(140, 105) == firefly::PanelGestureDecision::Dismiss,
                "panel gesture decision stays locked");
    arbiter.begin(100, 100);
    expect_true(arbiter.update(120, 95) == firefly::PanelGestureDecision::Ignore,
                "horizontal panel gesture stays with control");
    arbiter.begin(100, 100);
    expect_true(arbiter.update(100, 120) == firefly::PanelGestureDecision::Ignore,
                "downward panel gesture does not dismiss");
    arbiter.reset();
    expect_true(!arbiter.active(), "panel gesture reset clears activity");
}
```

并在 `setup()` 中调用该测试。

- [x] **Step 2: 运行核心测试构建并确认 RED**

Run: `powershell -ExecutionPolicy Bypass -File tools/build_firmware.ps1 -Target FireflyCoreTests`

Expected: 编译失败，原因是 `PanelGestureArbiter`/`PanelGestureDecision` 尚不存在。

- [x] **Step 3: 写最小仲裁器实现**

接口固定为：

```cpp
enum class PanelGestureDecision : uint8_t { Pending, Ignore, Dismiss };

class PanelGestureArbiter {
public:
    static constexpr int16_t kDirectionThreshold = 16;
    void begin(int16_t x, int16_t y);
    PanelGestureDecision update(int16_t x, int16_t y);
    void reset();
    bool active() const;
private:
    int16_t start_x_ = 0;
    int16_t start_y_ = 0;
    bool active_ = false;
    PanelGestureDecision decision_ = PanelGestureDecision::Pending;
};
```

`update()` 在未开始时返回 `Ignore`；最大轴位移不足 16px 时返回 `Pending`；`dy <= -16 && abs(dy) > abs(dx)` 时锁定 `Dismiss`；其他达到门槛的移动锁定 `Ignore`。

- [x] **Step 4: 暴露头文件并确认 GREEN**

在 `FireflyOS.h` 引入新头文件，然后重新运行核心测试构建。

Expected: `FireflyCoreTests` 编译成功。

### Task 2: 用仓库契约定义系统面板事件接线

**Files:**
- Modify: `tests/python/test_repository_contracts.py`

- [x] **Step 1: 写失败的事件接线契约**

增加两个测试，要求源码包含递归冒泡配置、父面板四个明确事件、独立的打开/关闭回调、`lv_indev_wait_release()` 和 `-LCD_HEIGHT`，并确保面板不再通过 `LV_EVENT_ALL` 注册旧 `status_drag_cb`：

```python
def test_system_panel_descendants_bubble_pointer_events(self):
    sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(encoding="utf-8", errors="ignore")
    self.assertIn("configure_panel_event_bubbling", sketch)
    self.assertIn("lv_obj_get_child_cnt", sketch)
    self.assertIn("LV_OBJ_FLAG_EVENT_BUBBLE", sketch)
    self.assertIn("configure_panel_event_bubbling(notif_panel);", sketch)

def test_system_panel_has_parent_owned_swipe_dismiss(self):
    sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(encoding="utf-8", errors="ignore")
    interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(encoding="utf-8", errors="ignore")
    for event in ("LV_EVENT_PRESSED", "LV_EVENT_PRESSING", "LV_EVENT_RELEASED", "LV_EVENT_PRESS_LOST"):
        self.assertIn(f"lv_obj_add_event_cb(notif_panel, system_panel_drag_cb, {event}", sketch)
    self.assertIn("status_open_drag_cb", sketch)
    self.assertNotIn("lv_obj_add_event_cb(notif_panel, status_drag_cb, LV_EVENT_ALL", sketch)
    self.assertIn("lv_indev_wait_release(indev)", interaction)
    self.assertIn("PanelGestureArbiter", interaction)
    self.assertIn("-LCD_HEIGHT", interaction)
```

- [x] **Step 2: 运行目标契约并确认 RED**

Run: `python -m unittest tests.python.test_repository_contracts.RepositoryContracts.test_system_panel_descendants_bubble_pointer_events tests.python.test_repository_contracts.RepositoryContracts.test_system_panel_has_parent_owned_swipe_dismiss -v`

Expected: 两项失败，失败原因为冒泡配置和父面板关闭回调尚未实现。

### Task 3: 实现父面板拥有的关闭手势

**Files:**
- Modify: `Firefly/FireflyApp.h`
- Modify: `Firefly/FireflyInteraction.cpp`
- Modify: `Firefly/Firefly.ino`

- [x] **Step 1: 递归配置完整冒泡链**

在 `Firefly.ino` 的匿名命名空间增加：

```cpp
void configure_panel_event_bubbling(lv_obj_t * parent) {
    if(!parent) return;
    const uint32_t child_count = lv_obj_get_child_cnt(parent);
    for(uint32_t index = 0; index < child_count; ++index) {
        lv_obj_t * child = lv_obj_get_child(parent, index);
        lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
        configure_panel_event_bubbling(child);
    }
}
```

在控制中心、通知中心和把手创建完成后调用一次；不为 `notif_panel` 自身添加冒泡标志。

- [x] **Step 2: 拆分打开和关闭回调**

将 `status_drag_cb` 拆为 `status_open_drag_cb` 与 `system_panel_drag_cb`。顶部状态栏只注册打开回调的四个指针事件；父面板只注册关闭回调的四个指针事件；把手不再单独注册关闭回调。

- [x] **Step 3: 接入关闭状态机并保护原控件**

在 `FireflyInteraction.cpp` 的 UI 回调侧保存原始命中对象、音量/亮度快照和 `PanelGestureArbiter`。父面板完全展开时才开始跟踪；`Dismiss` 时调用 `lv_indev_wait_release(indev)`、按需恢复音量模型/Preferences/UI 和亮度模型/硬件/UI，删除旧面板动画，并启动到 `-LCD_HEIGHT` 的 180ms ease-out 动画；`RELEASED`/`PRESS_LOST` 无条件复位。

- [x] **Step 4: 运行目标 Python 契约并确认 GREEN**

Run: `python -m unittest tests.python.test_repository_contracts.RepositoryContracts.test_system_panel_descendants_bubble_pointer_events tests.python.test_repository_contracts.RepositoryContracts.test_system_panel_has_parent_owned_swipe_dismiss -v`

Expected: 两项通过。

- [x] **Step 5: 构建主固件**

Run: `powershell -ExecutionPolicy Bypass -File tools/build_firmware.ps1 -Target Firefly`

Expected: 退出码 0，并输出程序存储空间和动态内存占用。

### Task 4: 同步能力边界并执行全量验证

**Files:**
- Modify: `docs/模块说明/01-UI-Shell.md`
- Modify: `docs/项目介绍-对话衔接版.md`
- Modify: `docs/执行计划/00-总体执行路线图.md`
- Modify: `docs/执行计划/02-UI-Shell与视觉系统.md`
- Modify: `docs/执行计划/02A-UI-Shell缺口补全.md`
- Modify: `knowledge/README.md`
- Modify: `knowledge/02-UI-Shell与导航覆盖层.md`
- Modify: `knowledge/14-主程序集成与后续复用顺序.md`
- Verify: `knowledge/validate_knowledge.py`

- [x] **Step 1: 更新实现状态但保留真机门禁**

文档应说明事件冒泡、方向仲裁、按钮/滑杆保护已经进入当前源码；同时明确自动测试和编译不能替代控制中心/通知中心的真机滑动矩阵，未取得真机证据前不得写成硬件验收通过。

- [x] **Step 2: 运行 Python 全集**

Run: `python -m unittest discover -s tests/python -v`

Expected: 若仍存在已知基线文档缺失，单独记录；本次新增系统面板契约必须通过。

- [x] **Step 3: 构建核心测试和主固件**

Run: `powershell -ExecutionPolicy Bypass -File tools/build_firmware.ps1 -Target FireflyCoreTests`

Run: `powershell -ExecutionPolicy Bypass -File tools/build_firmware.ps1 -Target Firefly`

Expected: 两个目标均退出 0。

- [x] **Step 4: 校验知识库与最终差异**

Run: `python knowledge/validate_knowledge.py`

Run: `git diff --check`

Run: `git status --short`

Expected: 知识库校验通过、无空白错误；差异只包含本计划列出的源码、测试和文档，不包含恢复、暂存或提交操作。

- [x] **Step 5: 记录真机待验矩阵**

交付说明必须保留以下待验项：控制中心与通知中心分别从背景、卡片、按钮、滑杆和通知项上滑；按钮轻点；滑杆横向调整；滑杆纵向上滑后的数值恢复；页面切换与触摸中断。每类 20 次，未实测前明确标记为待执行。

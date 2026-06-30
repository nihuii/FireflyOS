# FireflyOS UI Shell 与视觉系统 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在获得 410 × 502 预览批准后，建立统一主题、组件、导航和系统 UI Shell，并将现有锁屏、桌面、设置、控制中心与系统覆盖层渐进迁移。

**Architecture:** Shell 常驻并独占全局状态栏、导航、控制/通知中心和系统覆盖层；普通应用页面按需创建和销毁。主题通过固定令牌驱动，壁纸只在导入时采样一次，所有 LVGL 操作继续留在 UI 核。

**Tech Stack:** LVGL 8.3.11、Arduino_GFX、HTML/CSS 预览、RGB565/PNG 资源、Montserrat、简体中文字库子集。

---

## 1. 文件结构锁定

**Create:**

```text
docs/UI预览/01-Shell/index.html
docs/UI预览/01-Shell/资源预算.md
docs/UI预览/01-Shell/审批记录.md
libraries/FireflyOS/src/firefly/ui/UiTokens.h
libraries/FireflyOS/src/firefly/ui/UiTheme.h
libraries/FireflyOS/src/firefly/ui/UiTheme.cpp
libraries/FireflyOS/src/firefly/ui/UiComponents.h
libraries/FireflyOS/src/firefly/ui/UiComponents.cpp
libraries/FireflyOS/src/firefly/ui/Screen.h
libraries/FireflyOS/src/firefly/ui/NavigationController.h
libraries/FireflyOS/src/firefly/ui/NavigationController.cpp
libraries/FireflyOS/src/firefly/ui/UiShell.h
libraries/FireflyOS/src/firefly/ui/UiShell.cpp
libraries/FireflyOS/src/firefly/ui/screens/GlanceScreen.h
libraries/FireflyOS/src/firefly/ui/screens/GlanceScreen.cpp
libraries/FireflyOS/src/firefly/ui/screens/LockScreen.h
libraries/FireflyOS/src/firefly/ui/screens/LockScreen.cpp
libraries/FireflyOS/src/firefly/ui/screens/HomeScreen.h
libraries/FireflyOS/src/firefly/ui/screens/HomeScreen.cpp
libraries/FireflyOS/src/firefly/ui/screens/ControlCenter.h
libraries/FireflyOS/src/firefly/ui/screens/ControlCenter.cpp
libraries/FireflyOS/src/firefly/ui/screens/NotificationCenter.h
libraries/FireflyOS/src/firefly/ui/screens/NotificationCenter.cpp
libraries/FireflyOS/src/firefly/ui/screens/SystemOverlayHost.h
libraries/FireflyOS/src/firefly/ui/screens/SystemOverlayHost.cpp
tools/assets/check_ui_preview.py
docs/模块说明/01-UI-Shell.md
```

**Modify progressively:**

```text
Firefly/Firefly.ino:276-1081
Firefly/FireflyInteraction.cpp:165-217
Firefly/FireflyInteraction.cpp:483-704
Firefly/FireflyInteraction.cpp:755-783
Firefly/FireflyTheme.cpp:1-159
Firefly/FireflyApp.h:20-160
Firefly/FireflyState.cpp:10-108
```

## 2. Task 1：先完成 Shell 全局预览并获得批准

**Files:**
- Create: `docs/UI预览/01-Shell/index.html`
- Create: `docs/UI预览/01-Shell/资源预算.md`
- Create: `docs/UI预览/01-Shell/审批记录.md`
- Create: `tools/assets/check_ui_preview.py`

- [ ] **Step 1: 写预览检查测试**

  `tools/assets/check_ui_preview.py`：

  ```python
  from pathlib import Path
  import re

  root = Path(__file__).resolve().parents[2]
  html = (root / "docs" / "UI预览" / "01-Shell" / "index.html").read_text(
      encoding="utf-8"
  )

  required = [
      "glance-screen", "lock-screen", "home-screen",
      "control-center", "notification-center", "app-shell"
  ]
  missing = [name for name in required if f'id="{name}"' not in html]
  if missing:
      raise SystemExit(f"missing preview frames: {missing}")

  frames = re.findall(r'class="watch-frame"', html)
  if len(frames) < 6:
      raise SystemExit("at least six 410x502 frames are required")

  print("UI preview contract: PASS")
  ```

- [ ] **Step 2: 运行检查并确认失败**

  ```powershell
  python .\tools\assets\check_ui_preview.py
  ```

  Expected: FAIL，因为 HTML 尚未创建。

- [ ] **Step 3: 绘制精确尺寸 HTML 预览**

  `index.html` 必须包含六个 `width:410px;height:502px` 的 `.watch-frame`：

  - 一瞥界面：纯黑背景、200 × 200 中央主题图、时间和日期。
  - 锁屏：大时间、日期、下一闹钟、连接/电量摘要、上滑提示。
  - 桌面：三列 × 两行应用网格、分页点、状态栏。
  - 控制中心：亮度、音量、BLE、Wi-Fi、省电、屏幕锁定。
  - 通知中心：最多三张通知摘要卡、清除操作、空状态。
  - 应用 Shell：统一标题、滚动内容区、BOOT 返回提示语义。

  每个画框必须叠加：

  ```css
  .safe-area {
    position: absolute;
    left: 24px;
    top: 24px;
    width: 362px;
    height: 446px;
    border: 1px dashed rgba(255,255,255,.28);
    pointer-events: none;
  }
  ```

- [ ] **Step 4: 写资源预算**

  `资源预算.md` 为每个预览页列出：常驻 LVGL 对象数、图片尺寸/格式、是否全屏 Alpha、动画对象数、预计 Flash 和峰值临时 RAM。全屏 RGB565 按 411,640 字节/张计算。

- [ ] **Step 5: 运行预览检查**

  ```powershell
  python .\tools\assets\check_ui_preview.py
  ```

  Expected: `UI preview contract: PASS`。

- [ ] **Step 6: 用户审批 Gate**

  在浏览器展示预览，明确说明这一步尚未修改固件。将用户确认的日期、确认范围和要求调整项写入 `审批记录.md`。没有明确批准，不执行 Task 2 及其后任务。

- [ ] **Step 7: Commit**

  ```powershell
  git add docs/UI预览/01-Shell tools/assets/check_ui_preview.py
  git commit -m "docs: add approved FireflyOS shell previews"
  ```

## 3. Task 2：建立主题令牌和壁纸采样缓存

**Files:**
- Create: `libraries/FireflyOS/src/firefly/ui/UiTokens.h`
- Create: `libraries/FireflyOS/src/firefly/ui/UiTheme.h`
- Create: `libraries/FireflyOS/src/firefly/ui/UiTheme.cpp`
- Modify: `Firefly/FireflyTheme.cpp:1-159`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 写主题令牌测试**

  ```cpp
  static void test_default_theme_tokens() {
      const firefly::UiTokens tokens = firefly::UiTheme::fireflyDefault();
      expect_true(tokens.bg_base == 0x0041, "AMOLED base is near black");
      expect_true(tokens.radius_card == 24, "card radius token");
      expect_true(tokens.touch_min == 48, "minimum touch target");
  }
  ```

- [ ] **Step 2: 定义与 LVGL 解耦的令牌**

  `UiTokens.h`：

  ```cpp
  #pragma once
  #include <stdint.h>

  namespace firefly {
  struct UiTokens {
      uint16_t bg_base;
      uint16_t bg_surface;
      uint16_t firefly_primary;
      uint16_t firefly_secondary;
      uint16_t text_primary;
      uint16_t text_secondary;
      uint16_t sam_energy;
      uint16_t sam_ignition;
      uint16_t critical;
      uint8_t radius_card;
      uint8_t radius_button;
      uint8_t touch_min;
      uint8_t side_inset;
      uint8_t bottom_inset;
  };
  }
  ```

- [ ] **Step 3: 实现默认主题**

  `UiTheme` 提供 `fireflyDefault()` 和 `samAlert()`；颜色值使用脚本离线转换后的 RGB565 常量，不在页面创建时做字符串解析。

- [ ] **Step 4: 迁移现有取色算法**

  将 `FireflyTheme.cpp` 的纯采样逻辑迁移至 `UiTheme::sampleWallpaper()`，输入 RGB565 像素和尺寸，输出 `UiTokens`。采样只在主题导入/首次初始化时执行，并把结果写入 Preferences；页面显示只读取缓存令牌。

- [ ] **Step 5: 运行验证并提交**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/ui Firefly/FireflyTheme.cpp tests/FireflyCoreTests
  git commit -m "feat: add cached FireflyOS theme tokens"
  ```

## 4. Task 3：建立统一 LVGL 组件工厂

**Files:**
- Create: `libraries/FireflyOS/src/firefly/ui/UiComponents.h`
- Create: `libraries/FireflyOS/src/firefly/ui/UiComponents.cpp`
- Modify: `Firefly/Firefly.ino:300-380`

- [ ] **Step 1: 定义组件 API**

  ```cpp
  #pragma once
  #include <lvgl.h>
  #include "UiTokens.h"

  namespace firefly {
  class UiComponents {
  public:
      static lv_obj_t * createPage(lv_obj_t * parent, const UiTokens & tokens);
      static lv_obj_t * createCard(lv_obj_t * parent, const UiTokens & tokens);
      static lv_obj_t * createPrimaryButton(lv_obj_t * parent,
                                            const UiTokens & tokens,
                                            const char * text);
      static lv_obj_t * createTitle(lv_obj_t * parent,
                                    const UiTokens & tokens,
                                    const char * text);
      static void styleSlider(lv_obj_t * slider, const UiTokens & tokens);
      static void styleSwitch(lv_obj_t * sw, const UiTokens & tokens);
  };
  }
  ```

- [ ] **Step 2: 实现卡片与按钮样式**

  `createCard()` 必须设置：无阴影、1px 低透明边框、局部不透明背景、卡片圆角和统一内边距；`createPrimaryButton()` 强制高度至少 56px、可点击区域至少 48 × 48px。

- [ ] **Step 3: 替换原局部 lambda**

  把 `Firefly.ino` 中 `style_card`、`style_settings_card`、`style_slider`、`style_switch` 的调用逐个替换为组件工厂；每替换一类组件就编译一次，不一次性改完整文件。

- [ ] **Step 4: 真机视觉对比**

  对锁屏、设置菜单、闹钟列表和闹钟编辑页截图。Expected: 文本位置不变，交互热区不缩小，滑动无新增阴影或透明层负担。

- [ ] **Step 5: Commit**

  ```powershell
  git add libraries/FireflyOS/src/firefly/ui/UiComponents.* Firefly/Firefly.ino
  git commit -m "refactor: centralize LVGL component styling"
  ```

## 5. Task 4：实现导航栈

**Files:**
- Create: `libraries/FireflyOS/src/firefly/ui/NavigationController.h`
- Create: `libraries/FireflyOS/src/firefly/ui/NavigationController.cpp`
- Modify: `tests/FireflyCoreTests/FireflyCoreTests.ino`

- [ ] **Step 1: 写导航测试**

  ```cpp
  static void test_navigation_stack() {
      firefly::NavigationController nav;
      expect_true(nav.current() == firefly::Route::Lock, "starts locked");
      expect_true(nav.open(firefly::Route::Home), "open home");
      expect_true(nav.open(firefly::Route::Settings), "open settings");
      expect_true(nav.back() == firefly::Route::Home, "back to home");
      expect_true(nav.back() == firefly::Route::Lock, "home back locks");
  }
  ```

- [ ] **Step 2: 实现固定深度栈**

  ```cpp
  #pragma once
  #include <stdint.h>

  namespace firefly {
  enum class Route : uint8_t {
      Lock, Home, Settings, Clock, Calendar, Activity, Weather, Music,
      Recorder, Files, Themes, Tools, Diagnostics
  };

  class NavigationController {
  public:
      static constexpr uint8_t kDepth = 6;
      NavigationController();
      bool open(Route route);
      Route back();
      Route current() const;
      void lock();
  private:
      Route stack_[kDepth]{};
      uint8_t depth_ = 1;
  };
  }
  ```

  规则：初始 `Lock`；打开 `Home` 时重置为 `Lock -> Home`；应用可继续压栈；超过 6 层拒绝；桌面 back 返回锁屏。

- [ ] **Step 3: 运行验证并提交**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/ui/NavigationController.* tests/FireflyCoreTests
  git commit -m "feat: add bounded UI navigation stack"
  ```

## 6. Task 5：建立 UiShell 与系统层级

**Files:**
- Create: `libraries/FireflyOS/src/firefly/ui/UiShell.h`
- Create: `libraries/FireflyOS/src/firefly/ui/UiShell.cpp`
- Create: `libraries/FireflyOS/src/firefly/ui/screens/SystemOverlayHost.h`
- Create: `libraries/FireflyOS/src/firefly/ui/screens/SystemOverlayHost.cpp`
- Modify: `Firefly/Firefly.ino:276-299`

- [ ] **Step 1: 定义 Shell 所有权**

  ```cpp
  namespace firefly {
  class UiShell {
  public:
      bool create(lv_obj_t * screen, const UiTokens & tokens);
      void showRoute(Route route);
      void showControlCenter(bool visible);
      void showNotificationCenter(bool visible);
      void showOverlay(uint8_t priority, lv_obj_t * overlay);
      void closeOverlay(lv_obj_t * overlay);
      void refresh(const SystemState & state, uint32_t revision);
      NavigationController & navigation() { return navigation_; }
  private:
      NavigationController navigation_{};
      lv_obj_t * root_ = nullptr;
      lv_obj_t * app_host_ = nullptr;
      lv_obj_t * status_bar_ = nullptr;
      lv_obj_t * panel_host_ = nullptr;
      lv_obj_t * overlay_host_ = nullptr;
      uint32_t rendered_revision_ = 0;
  };
  }
  ```

- [ ] **Step 2: 创建固定 Z 顺序**

  从后到前固定为 `root_ -> app_host_ -> status_bar_ -> panel_host_ -> overlay_host_`。只允许 Shell 调用 `lv_obj_move_foreground()` 管理全局层级。

- [ ] **Step 3: 接入现有根屏幕**

  `build_firefly_os()` 仍创建 `scr_firefly`，随后调用 `ui_shell.create(scr_firefly, tokens)`；旧页面暂时挂入 `app_host_`，保持显示不变。

- [ ] **Step 4: 编译、真机检查并提交**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  git add libraries/FireflyOS/src/firefly/ui Firefly/Firefly.ino
  git commit -m "feat: add persistent FireflyOS UI shell"
  ```

## 7. Task 6：迁移一瞥、锁屏与桌面

**Files:**
- Create: `libraries/FireflyOS/src/firefly/ui/Screen.h`
- Create: `libraries/FireflyOS/src/firefly/ui/screens/GlanceScreen.*`
- Create: `libraries/FireflyOS/src/firefly/ui/screens/LockScreen.*`
- Create: `libraries/FireflyOS/src/firefly/ui/screens/HomeScreen.*`
- Modify: `Firefly/Firefly.ino:435-498`
- Modify: `Firefly/Firefly.ino:1053-1077`
- Modify: `Firefly/FireflyInteraction.cpp:165-185`
- Modify: `Firefly/FireflyInteraction.cpp:704-783`

- [ ] **Step 1: 为每个屏幕定义统一生命周期**

  ```cpp
  class Screen {
  public:
      virtual ~Screen() = default;
      virtual bool create(lv_obj_t * parent, const UiTokens & tokens) = 0;
      virtual void show() = 0;
      virtual void hide() = 0;
      virtual void refresh(const SystemState & state) = 0;
  };
  ```

- [ ] **Step 2: 迁移 GlanceScreen**

  保留四张 200 × 200 PNG 的轮换规则；黑屏唤醒不切图；将图片索引封装进 `GlanceScreen`，不再留在 `FireflyInteraction.cpp` 匿名命名空间。

- [ ] **Step 3: 迁移 LockScreen**

  保留壁纸、日期、时间、星期和上滑解锁；增加预览批准的下一闹钟/状态摘要，但这些新增控件必须在预览审批范围内。

- [ ] **Step 4: 迁移 HomeScreen**

  从 `AppRegistry` 读取应用描述，按三列两行分页创建图标；滑动锁屏进入桌面期间继续隐藏图标层，结束后再显示。

- [ ] **Step 5: 回归并提交**

  连续执行 20 次锁屏上滑、20 次 BOOT 返回、10 次息屏轮播。Expected: 无反向滑回锁屏、图片顺序正确、转场不比基线慢 15% 以上。

  ```powershell
  git add libraries/FireflyOS/src/firefly/ui/screens Firefly
  git commit -m "refactor: migrate glance lock and home screens"
  ```

## 8. Task 7：迁移控制中心、通知中心与系统覆盖层

**Files:**
- Create: `libraries/FireflyOS/src/firefly/ui/screens/ControlCenter.*`
- Create: `libraries/FireflyOS/src/firefly/ui/screens/NotificationCenter.*`
- Modify: `Firefly/Firefly.ino:499-610`
- Modify: `Firefly/Firefly.ino:1001-1051`
- Modify: `Firefly/FireflyInteraction.cpp:187-281`
- Modify: `Firefly/FireflyInteraction.cpp:589-703`

- [ ] **Step 1: 迁移顶部下拉面板**

  将旧 `notif_panel` 拆为同一 `panel_host_` 下的两个页签：控制和通知。下拉动画只移动一个不透明面板，时长 180ms。

- [ ] **Step 2: 绑定状态快照**

  `ControlCenter::refresh()` 只接收 `SystemState` 和音量/亮度值；不直接调用 PMU。电池和连接变化通过 StateStore revision 触发局部更新。

- [ ] **Step 3: 建立通知空状态**

  首版只实现本地通知列表模型和“暂无通知”；BLE 通知注入留给计划 5，但 UI 接口固定为 `setNotifications(const NotificationSummary*, uint8_t)`。

- [ ] **Step 4: 迁移覆盖层并固定优先级**

  `SystemOverlayHost` 使用 1–5 优先级；OTA/关机为 5、闹钟/极低电量为 4、授权/错误为 3、充电/通知为 2、普通弹窗为 1。低优先级不得覆盖高优先级。

- [ ] **Step 5: 运行覆盖层测试和真机回归**

  依次触发充电提示、闹钟和返回操作。Expected: 闹钟覆盖充电提示；关闭闹钟后恢复正确页面；控制中心不会盖住闹钟。

- [ ] **Step 6: Commit**

  ```powershell
  git add libraries/FireflyOS/src/firefly/ui/screens Firefly
  git commit -m "refactor: migrate system panels and overlays"
  ```

## 9. Task 8：中文字体与 AI 图标资源流程

**Files:**
- Create: `tools/assets/system_glyphs.txt`
- Create: `tools/assets/build_fonts.ps1`
- Create: `docs/模块说明/02-美术资源规范.md`
- Modify: `docs/UI预览/01-Shell/资源预算.md`

- [ ] **Step 1: 收集系统固定中文字形**

  扫描 `Firefly`、`libraries/FireflyOS/src` 中所有 UTF-8 字符串，将唯一中文字符写入 `system_glyphs.txt`；数字和 ASCII 继续使用 Montserrat。

- [ ] **Step 2: 生成字体子集**

  使用 LVGL 8 对应的 `lv_font_conv` 生成 18px、22px 和 24px 4bpp 字体；命令封装在 `build_fonts.ps1`，输出到 `Firefly/assets/fonts/`。

- [ ] **Step 3: 为每个应用生成美术提示词**

  在美术规范中为时钟、活动、天气、音乐、录音、文件、主题、设置、诊断分别填写：核心象征、主色、SAM 点缀、禁止元素和最终像素尺寸。统一使用总纲第 22 章提示词模板。

- [ ] **Step 4: 小尺寸验证**

  所有图标先在 512/1024px 生成，再缩放到 72 × 72 和 48 × 48；在黑色和 `bg_surface` 两种背景上检查辨识度。没有用户预览批准，不嵌入固件。

- [ ] **Step 5: Commit**

  ```powershell
  git add tools/assets docs/模块说明/02-美术资源规范.md docs/UI预览/01-Shell/资源预算.md
  git commit -m "build: add Chinese font and art asset pipeline"
  ```

## 10. Task 9：Gate B 验收

**Files:**
- Create: `docs/模块说明/01-UI-Shell.md`
- Modify: `docs/项目介绍.md`
- Modify: `Firefly/README.md`

- [ ] **Step 1: 运行完整验证**

  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\verify_all.ps1
  ```

- [ ] **Step 2: 真机验收**

  - 六张批准预览与真机截图逐项对照。
  - 所有主要触摸区至少 48 × 48px。
  - 右上、右下和底部控件不被圆角裁切。
  - 隐藏页面无活跃动画或刷新定时器。
  - 连续打开/关闭设置 50 次后可用内存回到初始值的 95% 以上。
  - 锁屏至桌面最大转场阻塞小于 250ms。

- [ ] **Step 3: 更新文档并提交**

  ```powershell
  git add docs/模块说明/01-UI-Shell.md docs/项目介绍.md Firefly/README.md
  git commit -m "docs: document FireflyOS UI shell"
  ```

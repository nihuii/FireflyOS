from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RepositoryContracts(unittest.TestCase):
    def test_main_sketch_exists(self):
        self.assertTrue((ROOT / "Firefly" / "Firefly.ino").is_file())

    def test_lvgl_configuration_exposes_image_cache_setting(self):
        text = (ROOT / "libraries" / "lv_conf.h").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("LV_IMG_CACHE_DEF_SIZE", text)

    def test_baseline_document_has_required_metrics(self):
        baseline = ROOT / "docs" / "模块说明" / "00-当前基线.md"
        self.assertTrue(baseline.is_file(), "baseline document must exist")
        text = baseline.read_text(encoding="utf-8")
        for name in ("固件大小", "内部 SRAM", "PSRAM", "启动时间", "400mAh"):
            self.assertIn(name, text)

    def test_main_sketch_emits_baseline_resource_log(self):
        text = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("FIREFLY_BASELINE", text)
        for field in ("startup_ms", "internal_free", "internal_min", "psram_free", "psram_size"):
            self.assertIn(field, text)

    def test_fireflyos_library_manifest_exists(self):
        manifest = ROOT / "libraries" / "FireflyOS" / "library.properties"
        self.assertTrue(manifest.is_file(), "FireflyOS library manifest must exist")
        self.assertIn("name=FireflyOS", manifest.read_text(encoding="utf-8"))

    def test_background_work_uses_event_bus(self):
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertNotIn("firefly_background_events", interaction)
        self.assertIn("firefly::EventBus system_event_bus", interaction)
        self.assertIn("void firefly_process_system_events()", interaction)
        self.assertIn("firefly_process_system_events();", sketch)

    def test_runtime_rtc_and_power_reads_use_hal(self):
        sources = "\n".join(
            (ROOT / "Firefly" / name).read_text(encoding="utf-8", errors="ignore")
            for name in ("Firefly.ino", "FireflyInteraction.cpp")
        )
        forbidden = (
            "rtc.getDateTime(",
            "rtc.setDateTime(",
            "power.getBatteryPercent(",
            "power.getTemperature(",
            "power.getBattVoltage(",
            "power.getSystemVoltage(",
            "power.isCharging(",
            "power.isVbusIn(",
            "gfx_co5300->setBrightness(",
        )
        for expression in forbidden:
            self.assertNotIn(expression, sources)
        self.assertIn("firefly_board.readBattery()", sources)
        self.assertIn("firefly_board.readEpoch(", sources)
        self.assertIn("firefly_board.writeEpoch(", sources)

    def test_gate_a_diagnostics_and_core_boundaries(self):
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8", errors="ignore"
        )
        background = interaction.split("void firefly_background_task", 1)[1].split(
            "String two_digit_text", 1
        )[0]
        self.assertNotIn("lv_", background)
        self.assertIn("FIREFLY_GATE_A", interaction)
        self.assertIn("firefly_report_gate_a_diagnostics();", sketch)
        self.assertIn("LV_EVENT_RELEASED", sketch)
        self.assertIn("event_post_failures", interaction)

        core_dir = ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "core"
        core_text = "\n".join(
            path.read_text(encoding="utf-8", errors="ignore")
            for path in core_dir.iterdir()
            if path.suffix in (".h", ".cpp")
        )
        self.assertNotIn("<lvgl", core_text.lower())
        self.assertNotIn("lv_obj_", core_text)

    def test_ui_component_styles_are_centralized(self):
        component_header = (
            ROOT
            / "libraries"
            / "FireflyOS"
            / "src"
            / "firefly"
            / "ui"
            / "UiComponents.h"
        )
        self.assertTrue(component_header.is_file())
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8", errors="ignore"
        )
        for local_style in (
            "auto style_card",
            "auto style_settings_card",
            "auto style_slider",
            "auto style_switch",
        ):
            self.assertNotIn(local_style, sketch)
        self.assertIn("UiComponents::styleCard", sketch)
        self.assertIn("UiComponents::styleSlider", sketch)
        self.assertIn("UiComponents::styleSwitch", sketch)

    def test_ui_shell_limits_and_placeholder_pipeline(self):
        navigation = (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "ui" /
                      "NavigationController.h").read_text(encoding="utf-8")
        notifications = (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "ui" /
                         "screens" / "NotificationCenter.h").read_text(encoding="utf-8")
        tokens = (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "ui" /
                  "UiTokens.h").read_text(encoding="utf-8")
        art = (ROOT / "docs" / "模块说明" / "02-美术资源规范.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("kDepth = 6", navigation)
        self.assertIn("kVisibleLimit = 3", notifications)
        self.assertIn("touch_min", tokens)
        self.assertIn("72 × 72", art)
        self.assertIn("LVGL Symbol", art)
        self.assertTrue((ROOT / "tools" / "assets" / "system_glyphs.txt").exists())
        self.assertTrue((ROOT / "tools" / "assets" / "build_fonts.ps1").exists())

    def test_ui_shell_routes_drive_real_pages(self):
        shell = (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "ui" /
                 "UiShell.h").read_text(encoding="utf-8")
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8", errors="ignore"
        )
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("RouteHandler", shell)
        self.assertIn("setRouteHandler", shell)
        self.assertIn("Route back()", shell)
        self.assertIn("bindPanelPages", shell)
        self.assertIn("bringAppToFront", shell)
        self.assertIn("handle_shell_route", sketch)
        self.assertTrue(
            (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "ui" /
             "screens" / "AppShellScreen.h").is_file()
        )
        self.assertIn("app_shell_screen", sketch)
        self.assertIn("ui_shell.back();", sketch)
        self.assertIn("ui_shell.back()", interaction)
        self.assertNotIn("lv_obj_move_foreground(sleep_screen)", interaction)

    def test_system_panels_are_peers_and_touch_targets_are_large_enough(self):
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8", errors="ignore"
        )
        controls = (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "ui" /
                    "screens" / "ControlCenter.h").read_text(encoding="utf-8")
        notifications = (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
                         "ui" / "screens" / "NotificationCenter.h").read_text(
                             encoding="utf-8"
                         )
        self.assertIn("lv_obj_t * root() const", controls)
        self.assertIn("void clear()", notifications)
        self.assertIn("clear_notifications_cb", sketch)
        self.assertIn("ui_shell.bindPanelPages", sketch)
        self.assertIn("lv_obj_set_ext_click_area(notif_volume_slider, 12)", sketch)
        self.assertIn("lv_obj_set_ext_click_area(notif_brightness_slider, 12)", sketch)

    def test_lock_screen_is_bound_to_next_alarm(self):
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("firefly_alarm_find_next", interaction)
        self.assertIn("lock_screen.setNextAlarm", interaction)

    def test_home_pager_updates_its_position_indicator(self):
        header = (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "ui" /
                  "screens" / "HomeScreen.h").read_text(encoding="utf-8")
        source = (ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "ui" /
                  "screens" / "HomeScreen.cpp").read_text(encoding="utf-8")
        self.assertIn("pagerEventCallback", header)
        self.assertIn("updateDots", source)
        self.assertIn("LV_EVENT_VALUE_CHANGED", source)


if __name__ == "__main__":
    unittest.main()

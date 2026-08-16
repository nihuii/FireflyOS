import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ANDROID = ROOT / "AndroidCompanion"


def read_required(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing Android companion artifact: {path}")
    return path.read_text(encoding="utf-8")


class AndroidCompanionScaffoldContractTests(unittest.TestCase):
    def test_gradle_project_uses_agp9_builtin_kotlin_and_views(self):
        settings = read_required(ANDROID / "settings.gradle.kts")
        root_build = read_required(ANDROID / "build.gradle.kts")
        app_build = read_required(ANDROID / "app" / "build.gradle.kts")
        wrapper = read_required(ANDROID / "gradle" / "wrapper" / "gradle-wrapper.properties")

        self.assertIn('rootProject.name = "FireflyOSAndroidCompanion"', settings)
        self.assertIn('id("com.android.application") version "9.1.0" apply false', root_build)
        self.assertNotIn("org.jetbrains.kotlin.android", root_build + app_build)
        self.assertNotIn("kotlin-android", root_build + app_build)
        self.assertNotIn("android.builtInKotlin=false", root_build + app_build)
        self.assertIn("compileSdk = 36", app_build)
        self.assertIn("minSdk = 26", app_build)
        self.assertIn("targetSdk = 36", app_build)
        self.assertIn("viewBinding = true", app_build)
        self.assertNotIn("+", root_build + app_build)
        self.assertNotIn("compose", (root_build + app_build).lower())
        self.assertIn("mirrors.cloud.tencent.com/gradle/gradle-9.3.1-all.zip", wrapper)

    def test_gradle_project_prefers_mainland_china_mirrors(self):
        settings = read_required(ANDROID / "settings.gradle.kts")
        wrapper = read_required(ANDROID / "gradle" / "wrapper" / "gradle-wrapper.properties")

        self.assertIn("mirrors.cloud.tencent.com/gradle/gradle-9.3.1-all.zip", wrapper)
        self.assertIn("maven.aliyun.com/repository/google", settings)
        self.assertIn("maven.aliyun.com/repository/public", settings)
        self.assertIn("maven.aliyun.com/repository/gradle-plugin", settings)
        self.assertLess(settings.index("maven.aliyun.com/repository/google"), settings.index("google()"))

    def test_manifest_declares_ble_calendar_and_local_transfer_permissions(self):
        manifest = read_required(ANDROID / "app" / "src" / "main" / "AndroidManifest.xml")
        for token in (
            'android.hardware.bluetooth_le',
            'android:required="true"',
            'android.permission.BLUETOOTH_SCAN',
            'android:usesPermissionFlags="neverForLocation"',
            'android.permission.BLUETOOTH_CONNECT',
            'android.permission.BLUETOOTH"',
            'android:maxSdkVersion="30"',
            'android.permission.BLUETOOTH_ADMIN',
            'android.permission.ACCESS_FINE_LOCATION',
            'android.permission.READ_CALENDAR',
            'android.permission.INTERNET',
            'android.permission.ACCESS_NETWORK_STATE',
            'android.permission.CHANGE_NETWORK_STATE',
            'android.permission.NEARBY_WIFI_DEVICES',
            'android:theme="@style/Theme.FireflyCompanion"',
        ):
            self.assertIn(token, manifest)

        self.assertNotIn("ACCESS_BACKGROUND_LOCATION", manifest)
        fine_location = manifest[manifest.index("ACCESS_FINE_LOCATION"):]
        fine_location = fine_location[:fine_location.index("/>")]
        self.assertIn('android:maxSdkVersion="30"', fine_location)

    def test_empty_views_activity_and_layout_explain_permission_boundaries(self):
        activity = read_required(
            ANDROID
            / "app"
            / "src"
            / "main"
            / "java"
            / "com"
            / "fireflyos"
            / "companion"
            / "MainActivity.kt"
        )
        layout = read_required(ANDROID / "app" / "src" / "main" / "res" / "layout" / "activity_main.xml")
        strings = read_required(ANDROID / "app" / "src" / "main" / "res" / "values" / "strings.xml")

        self.assertIn("android.app.Activity", activity)
        self.assertIn("class MainActivity : Activity()", activity)
        self.assertIn("ActivityMainBinding", activity)
        self.assertIn("Nearby Devices", strings)
        self.assertIn("READ_CALENDAR", strings)
        self.assertIn("扫描只用于用户明确的配对流程", strings)
        self.assertIn("android:minHeight=\"48dp\"", layout)
        self.assertIn("@+id/scanPairButton", layout)

        package_hits = re.findall(r"package com\.fireflyos\.companion", activity)
        self.assertEqual(1, len(package_hits))

    def test_bulk_transfer_has_one_idempotent_user_cancel_path(self):
        activity = read_required(
            ANDROID / "app" / "src" / "main" / "java" / "com" /
            "fireflyos" / "companion" / "MainActivity.kt"
        )
        controller = read_required(
            ANDROID / "app" / "src" / "main" / "java" / "com" /
            "fireflyos" / "companion" / "sync" / "CompanionController.kt"
        )
        layout = read_required(
            ANDROID / "app" / "src" / "main" / "res" / "layout" /
            "activity_main.xml"
        )
        cancel_id = 'android:id="@+id/bulkCancelButton"'
        self.assertIn(cancel_id, layout)
        cancel_start = layout.index(cancel_id)
        cancel_end = layout.index("</Button>", cancel_start)
        self.assertIn('android:minHeight="48dp"', layout[cancel_start:cancel_end])
        for token in (
            "private var bulkTransferJob: Job?",
            "private var activeBulkRequestId",
            "private fun cancelBulkTransfer(",
            "bulkTransferJob?.cancel()",
            "releaseBulkNetwork()",
            "companionController?.cancelBulkTransfer",
        ):
            self.assertIn(token, activity)
        self.assertGreaterEqual(activity.count("cancelBulkTransfer("), 4)
        self.assertIn("fun cancelBulkTransfer(requestId: Int)", controller)
        self.assertIn("hashedBytes > MAX_BULK_FILE_BYTES", activity)
        self.assertIn("hashedBytes != metadata.second", activity)
        self.assertIn("private var bulkOperationGeneration", activity)
        self.assertIn("generation != bulkOperationGeneration", activity)
        activity_result = activity[activity.index("override fun onActivityResult"):]
        self.assertIn("bulkTransferJob?.isActive == true", activity_result)
        self.assertIn("cancelBulkTransfer(", activity_result)
        self.assertGreaterEqual(
            activity.count("pendingBulkSession?.requestId == session.requestId"),
            3,
        )
        uploader = read_required(
            ANDROID / "app" / "src" / "main" / "java" / "com" /
            "fireflyos" / "companion" / "transfer" / "BulkTransfer.kt"
        )
        self.assertIn("onCancelling = true", uploader)
        self.assertIn("connection.disconnect()", uploader)
        self.assertIn('managedPath.endsWith(".part")', uploader)


if __name__ == "__main__":
    unittest.main()

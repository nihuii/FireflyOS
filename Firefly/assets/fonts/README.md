# 字体输出目录

此目录当前只保留占位说明，不含字体二进制或生成的 C 源码。

设置环境变量 `FIREFLY_FONT_SOURCE` 为已确认授权的中文 TTF/OTF 字体，再运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\assets\build_fonts.ps1
```

脚本生成 `firefly_cn_18.c`、`firefly_cn_22.c`、`firefly_cn_24.c`。字体许可证必须允许嵌入固件与再分发。

每次构建前脚本会运行 `tools/assets/collect_system_glyphs.py`，将人工保留字形与源码内的 CJK 字符合并去重。未提供授权字体时，系统继续使用 LVGL 内置字体作为占位，不会联网下载资源。

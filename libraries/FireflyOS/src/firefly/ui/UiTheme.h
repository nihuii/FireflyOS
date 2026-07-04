#pragma once

#include <stddef.h>
#include <stdint.h>

#include "UiTokens.h"

namespace firefly {

class UiTheme {
public:
    static UiTokens fireflyDefault();
    static UiTokens fromPalette(const uint32_t palette[5]);
    static void setRuntime(const UiTokens & tokens);
    static UiTokens samAlert();
    static UiTokens sampleWallpaper(const uint16_t * pixels,
                                    uint16_t width,
                                    uint16_t height);
};

}  // namespace firefly

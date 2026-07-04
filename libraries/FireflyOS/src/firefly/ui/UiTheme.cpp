#include "UiTheme.h"

namespace firefly {
namespace {

UiTokens builtInTokens() {
    return UiTokens{
        0x0041, 0x10E4, 0x56F5, 0x8E7A, 0xF7DF, 0xA5B6,
        0xAFE6, 0xFC40, 0xF986,
        24, 18, 48, 24, 32
    };
}

UiTokens runtime_tokens = builtInTokens();

uint16_t rgb888To565(uint32_t color) {
    const uint8_t red = static_cast<uint8_t>((color >> 16) & 0xFF);
    const uint8_t green = static_cast<uint8_t>((color >> 8) & 0xFF);
    const uint8_t blue = static_cast<uint8_t>(color & 0xFF);
    return static_cast<uint16_t>(((red & 0xF8U) << 8) |
                                 ((green & 0xFCU) << 3) |
                                 (blue >> 3));
}

struct RgbAccumulator {
    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
    uint32_t count = 0;

    void add(uint8_t r, uint8_t g, uint8_t b) {
        red += r;
        green += g;
        blue += b;
        ++count;
    }
};

uint8_t red5(uint16_t color) {
    const uint8_t value = static_cast<uint8_t>((color >> 11) & 0x1F);
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint8_t green6(uint16_t color) {
    const uint8_t value = static_cast<uint8_t>((color >> 5) & 0x3F);
    return static_cast<uint8_t>((value << 2) | (value >> 4));
}

uint8_t blue5(uint16_t color) {
    const uint8_t value = static_cast<uint8_t>(color & 0x1F);
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint16_t pack565(uint8_t red, uint8_t green, uint8_t blue) {
    return static_cast<uint16_t>(((red & 0xF8U) << 8) |
                                 ((green & 0xFCU) << 3) |
                                 (blue >> 3));
}

uint16_t average(const RgbAccumulator & sample, uint16_t fallback) {
    if(sample.count == 0) {
        return fallback;
    }
    return pack565(static_cast<uint8_t>(sample.red / sample.count),
                   static_cast<uint8_t>(sample.green / sample.count),
                   static_cast<uint8_t>(sample.blue / sample.count));
}

uint16_t mix(uint16_t foreground, uint16_t background, uint8_t weight) {
    const uint16_t inverse = 255U - weight;
    return pack565(
        static_cast<uint8_t>((red5(foreground) * weight + red5(background) * inverse + 127U) / 255U),
        static_cast<uint8_t>((green6(foreground) * weight + green6(background) * inverse + 127U) / 255U),
        static_cast<uint8_t>((blue5(foreground) * weight + blue5(background) * inverse + 127U) / 255U)
    );
}

uint8_t luminance(uint8_t red, uint8_t green, uint8_t blue) {
    return static_cast<uint8_t>((red * 77U + green * 150U + blue * 29U) >> 8);
}

}  // namespace

UiTokens UiTheme::fireflyDefault() {
    return runtime_tokens;
}

UiTokens UiTheme::fromPalette(const uint32_t palette[5]) {
    UiTokens tokens = runtime_tokens;
    if(!palette) return tokens;
    tokens.bg_base = rgb888To565(palette[0]);
    tokens.bg_surface = rgb888To565(palette[1]);
    tokens.firefly_primary = rgb888To565(palette[2]);
    tokens.firefly_secondary = rgb888To565(palette[3]);
    tokens.critical = rgb888To565(palette[4]);
    tokens.sam_energy = tokens.firefly_primary;
    return tokens;
}

void UiTheme::setRuntime(const UiTokens & tokens) {
    runtime_tokens = tokens;
}

UiTokens UiTheme::samAlert() {
    UiTokens tokens = fireflyDefault();
    tokens.bg_surface = 0x20C2;
    tokens.sam_energy = 0xCFE0;
    tokens.sam_ignition = 0xFA20;
    tokens.critical = 0xF800;
    return tokens;
}

UiTokens UiTheme::sampleWallpaper(const uint16_t * pixels,
                                  uint16_t width,
                                  uint16_t height) {
    UiTokens tokens = fireflyDefault();
    if(!pixels || width == 0 || height == 0) {
        return tokens;
    }

    RgbAccumulator all;
    RgbAccumulator accents;
    RgbAccumulator shadows;
    RgbAccumulator highlights;
    const uint16_t step_x = width > 18 ? width / 18 : 1;
    const uint16_t step_y = height > 22 ? height / 22 : 1;

    for(uint16_t y = 0; y < height; y = static_cast<uint16_t>(y + step_y)) {
        for(uint16_t x = 0; x < width; x = static_cast<uint16_t>(x + step_x)) {
            const uint16_t color = pixels[static_cast<uint32_t>(y) * width + x];
            const uint8_t red = red5(color);
            const uint8_t green = green6(color);
            const uint8_t blue = blue5(color);
            const uint8_t maximum = red > green ? (red > blue ? red : blue)
                                                 : (green > blue ? green : blue);
            const uint8_t minimum = red < green ? (red < blue ? red : blue)
                                                 : (green < blue ? green : blue);
            const uint8_t light = luminance(red, green, blue);
            all.add(red, green, blue);
            if(maximum - minimum > 36 && light > 48 && light < 220) {
                accents.add(red, green, blue);
            }
            if(light < 110) {
                shadows.add(red, green, blue);
            }
            if(light > 164) {
                highlights.add(red, green, blue);
            }
        }
    }

    const uint16_t average_color = average(all, tokens.firefly_secondary);
    const uint16_t accent = average(accents, average_color);
    const uint16_t shadow = average(shadows, tokens.bg_surface);
    const uint16_t highlight = average(highlights, tokens.text_primary);
    tokens.bg_surface = mix(shadow, tokens.bg_base, 120);
    tokens.firefly_primary = mix(highlight, accent, 104);
    tokens.firefly_secondary = mix(accent, tokens.bg_surface, 116);
    tokens.text_secondary = mix(tokens.text_primary, tokens.firefly_primary, 190);
    tokens.sam_energy = mix(tokens.firefly_primary, 0xCFE0, 96);
    return tokens;
}

}  // namespace firefly

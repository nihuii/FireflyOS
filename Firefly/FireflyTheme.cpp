#include "FireflyApp.h"

namespace {

struct ThemeRgbSample {
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    uint32_t count = 0;

    void add(uint8_t red, uint8_t green, uint8_t blue) {
        r += red;
        g += green;
        b += blue;
        ++count;
    }
};

uint8_t expand_5bit(uint8_t value) {
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint8_t expand_6bit(uint8_t value) {
    return static_cast<uint8_t>((value << 2) | (value >> 4));
}

uint8_t color_r(lv_color_t color) {
    return expand_5bit(LV_COLOR_GET_R(color));
}

uint8_t color_g(lv_color_t color) {
    return expand_6bit(LV_COLOR_GET_G(color));
}

uint8_t color_b(lv_color_t color) {
    return expand_5bit(LV_COLOR_GET_B(color));
}

lv_color_t rgb_color(uint8_t red, uint8_t green, uint8_t blue) {
    return lv_color_make(red, green, blue);
}

lv_color_t mix_color(lv_color_t foreground, lv_color_t background, uint8_t foreground_weight) {
    const uint16_t background_weight = 255U - foreground_weight;
    const uint8_t red = static_cast<uint8_t>((color_r(foreground) * foreground_weight + color_r(background) * background_weight + 127U) / 255U);
    const uint8_t green = static_cast<uint8_t>((color_g(foreground) * foreground_weight + color_g(background) * background_weight + 127U) / 255U);
    const uint8_t blue = static_cast<uint8_t>((color_b(foreground) * foreground_weight + color_b(background) * background_weight + 127U) / 255U);
    return rgb_color(red, green, blue);
}

uint8_t luminance_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return static_cast<uint8_t>((red * 77U + green * 150U + blue * 29U) >> 8);
}

uint8_t luminance_color(lv_color_t color) {
    return luminance_rgb(color_r(color), color_g(color), color_b(color));
}

lv_color_t average_or_fallback(const ThemeRgbSample & sample, lv_color_t fallback) {
    if(sample.count == 0) {
        return fallback;
    }

    return rgb_color(
        static_cast<uint8_t>(sample.r / sample.count),
        static_cast<uint8_t>(sample.g / sample.count),
        static_cast<uint8_t>(sample.b / sample.count)
    );
}

void sample_wallpaper_theme(const lv_img_dsc_t * wallpaper,
                           lv_color_t & average,
                           lv_color_t & accent,
                           lv_color_t & shadow,
                           lv_color_t & highlight) {
    average = lv_color_hex(0x7B6F93);
    accent = lv_color_hex(0xC68AE0);
    shadow = lv_color_hex(0x252843);
    highlight = lv_color_hex(0xF0D7F5);

    if(!wallpaper || !wallpaper->data || wallpaper->data_size < 2U) {
        return;
    }

    const uint32_t width = wallpaper->header.w;
    const uint32_t height = wallpaper->header.h;
    if(width == 0U || height == 0U) {
        return;
    }

    const uint8_t * pixels = wallpaper->data;
    const uint32_t step_x = width > 18U ? width / 18U : 1U;
    const uint32_t step_y = height > 22U ? height / 22U : 1U;

    ThemeRgbSample all_pixels;
    ThemeRgbSample accent_pixels;
    ThemeRgbSample shadow_pixels;
    ThemeRgbSample highlight_pixels;

    for(uint32_t y = 0; y < height; y += step_y) {
        for(uint32_t x = 0; x < width; x += step_x) {
            const uint32_t index = (y * width + x) * 2U;
            if(index + 1U >= wallpaper->data_size) {
                continue;
            }

            const uint16_t packed = static_cast<uint16_t>(pixels[index] | (static_cast<uint16_t>(pixels[index + 1U]) << 8));
            const uint8_t red = expand_5bit((packed >> 11) & 0x1FU);
            const uint8_t green = expand_6bit((packed >> 5) & 0x3FU);
            const uint8_t blue = expand_5bit(packed & 0x1FU);
            const uint8_t max_channel = max(red, max(green, blue));
            const uint8_t min_channel = min(red, min(green, blue));
            const uint8_t saturation = static_cast<uint8_t>(max_channel - min_channel);
            const uint8_t luminance = luminance_rgb(red, green, blue);

            all_pixels.add(red, green, blue);

            if(saturation > 36U && luminance > 48U && luminance < 220U) {
                accent_pixels.add(red, green, blue);
            }
            if(luminance < 110U) {
                shadow_pixels.add(red, green, blue);
            }
            if(luminance > 164U) {
                highlight_pixels.add(red, green, blue);
            }
        }
    }

    average = average_or_fallback(all_pixels, average);
    shadow = average_or_fallback(shadow_pixels, shadow);
    accent = average_or_fallback(accent_pixels, average);
    highlight = average_or_fallback(highlight_pixels, mix_color(lv_color_white(), accent, 110));
}

} // namespace

void init_settings_theme_from_wallpaper(const lv_img_dsc_t * wallpaper) {
    lv_color_t average;
    lv_color_t accent;
    lv_color_t shadow;
    lv_color_t highlight;
    sample_wallpaper_theme(wallpaper, average, accent, shadow, highlight);

    lv_color_t surface_base = mix_color(average, shadow, 82);
    settings_theme_surface = mix_color(surface_base, lv_color_black(), 170);
    settings_theme_surface_alt = mix_color(accent, settings_theme_surface, 92);
    settings_theme_accent = mix_color(highlight, accent, 104);
    settings_theme_action = mix_color(accent, shadow, 168);

    settings_theme_text_primary = luminance_color(settings_theme_surface) < 118
        ? lv_color_hex(0xF8FBFF)
        : lv_color_hex(0x13202A);
    settings_theme_text_secondary = mix_color(settings_theme_text_primary, settings_theme_accent, 204);
}

void init_default_settings_theme() {
    init_settings_theme_from_wallpaper(&settings_wallpaper_firefly_2);
}

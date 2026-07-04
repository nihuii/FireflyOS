#include "UiComponents.h"

namespace firefly {
namespace {

lv_color_t colorFrom565(uint16_t value) {
    const uint8_t red = static_cast<uint8_t>((value >> 11) & 0x1F);
    const uint8_t green = static_cast<uint8_t>((value >> 5) & 0x3F);
    const uint8_t blue = static_cast<uint8_t>(value & 0x1F);
    return lv_color_make(static_cast<uint8_t>((red << 3) | (red >> 2)),
                         static_cast<uint8_t>((green << 2) | (green >> 4)),
                         static_cast<uint8_t>((blue << 3) | (blue >> 2)));
}

uint32_t colorDistance(lv_color_t left, lv_color_t right) {
    const int32_t red = static_cast<int32_t>(LV_COLOR_GET_R(left)) -
                        LV_COLOR_GET_R(right);
    const int32_t green = static_cast<int32_t>(LV_COLOR_GET_G(left)) -
                          LV_COLOR_GET_G(right);
    const int32_t blue = static_cast<int32_t>(LV_COLOR_GET_B(left)) -
                         LV_COLOR_GET_B(right);
    return static_cast<uint32_t>(red * red + green * green + blue * blue);
}

bool remapSemanticColor(lv_color_t current,
                        const UiTokens & previous,
                        const UiTokens & next,
                        lv_color_t & mapped) {
    const uint16_t old_values[] = {
        previous.bg_base, previous.bg_surface, previous.firefly_primary,
        previous.firefly_secondary, previous.critical,
    };
    const uint16_t new_values[] = {
        next.bg_base, next.bg_surface, next.firefly_primary,
        next.firefly_secondary, next.critical,
    };
    uint32_t best_distance = UINT32_MAX;
    uint8_t best = 0;
    for(uint8_t i = 0; i < 5; ++i) {
        const uint32_t distance = colorDistance(current, colorFrom565(old_values[i]));
        if(distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    if(best_distance > 3600) return false;
    mapped = colorFrom565(new_values[best]);
    return true;
}

void applySelector(lv_obj_t * object,
                   lv_style_selector_t selector,
                   const UiTokens & previous,
                   const UiTokens & next) {
    lv_color_t mapped{};
    if(remapSemanticColor(lv_obj_get_style_bg_color(object, selector),
                          previous, next, mapped)) {
        lv_obj_set_style_bg_color(object, mapped, selector);
    }
    if(remapSemanticColor(lv_obj_get_style_text_color(object, selector),
                          previous, next, mapped)) {
        lv_obj_set_style_text_color(object, mapped, selector);
    }
    if(remapSemanticColor(lv_obj_get_style_border_color(object, selector),
                          previous, next, mapped)) {
        lv_obj_set_style_border_color(object, mapped, selector);
    }
    if(remapSemanticColor(lv_obj_get_style_outline_color(object, selector),
                          previous, next, mapped)) {
        lv_obj_set_style_outline_color(object, mapped, selector);
    }
    if(remapSemanticColor(lv_obj_get_style_arc_color(object, selector),
                          previous, next, mapped)) {
        lv_obj_set_style_arc_color(object, mapped, selector);
    }
    if(remapSemanticColor(lv_obj_get_style_line_color(object, selector),
                          previous, next, mapped)) {
        lv_obj_set_style_line_color(object, mapped, selector);
    }
    if(remapSemanticColor(lv_obj_get_style_shadow_color(object, selector),
                          previous, next, mapped)) {
        lv_obj_set_style_shadow_color(object, mapped, selector);
    }
    if(remapSemanticColor(lv_obj_get_style_img_recolor(object, selector),
                          previous, next, mapped)) {
        lv_obj_set_style_img_recolor(object, mapped, selector);
    }
}

}  // namespace

lv_obj_t * UiComponents::createPage(lv_obj_t * parent, const UiTokens & tokens) {
    lv_obj_t * page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(page, tokens.side_inset, 0);
    lv_obj_set_style_bg_color(page, colorFrom565(tokens.bg_base), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    return page;
}

lv_obj_t * UiComponents::createCard(lv_obj_t * parent, const UiTokens & tokens) {
    lv_obj_t * card = lv_obj_create(parent);
    styleSettingsCard(card,
                      colorFrom565(tokens.bg_surface),
                      tokens.radius_card,
                      colorFrom565(tokens.firefly_primary),
                      LV_OPA_90);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    return card;
}

lv_obj_t * UiComponents::createPrimaryButton(lv_obj_t * parent,
                                             const UiTokens & tokens,
                                             const char * text) {
    lv_obj_t * button = lv_btn_create(parent);
    lv_obj_set_size(button, LV_PCT(100), 56);
    styleCard(button,
              colorFrom565(tokens.firefly_primary),
              tokens.radius_button,
              LV_OPA_COVER);
    lv_obj_set_style_min_width(button, tokens.touch_min, 0);
    lv_obj_set_style_min_height(button, tokens.touch_min, 0);
    lv_obj_t * label = lv_label_create(button);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, colorFrom565(tokens.bg_base), 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t * UiComponents::createTitle(lv_obj_t * parent,
                                     const UiTokens & tokens,
                                     const char * text) {
    lv_obj_t * title = lv_label_create(parent);
    lv_label_set_text(title, text ? text : "");
    lv_obj_set_style_text_color(title, colorFrom565(tokens.text_primary), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    return title;
}

void UiComponents::styleCard(lv_obj_t * obj,
                             lv_color_t color,
                             lv_coord_t radius,
                             lv_opa_t opacity) {
    if(!obj) return;
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opacity, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

void UiComponents::styleSettingsCard(lv_obj_t * obj,
                                     lv_color_t color,
                                     lv_coord_t radius,
                                     lv_color_t accent,
                                     lv_opa_t opacity) {
    styleCard(obj, color, radius, opacity);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, accent, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_30, 0);
}

void UiComponents::styleSlider(lv_obj_t * slider, const UiTokens & tokens) {
    styleSlider(slider,
                colorFrom565(tokens.bg_surface),
                colorFrom565(tokens.firefly_primary),
                colorFrom565(tokens.text_primary));
}

void UiComponents::styleSlider(lv_obj_t * slider,
                               lv_color_t track,
                               lv_color_t indicator,
                               lv_color_t knob) {
    if(!slider) return;
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, indicator, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, knob, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
}

void UiComponents::styleSwitch(lv_obj_t * sw, const UiTokens & tokens) {
    styleSwitch(sw,
                colorFrom565(tokens.bg_surface),
                colorFrom565(tokens.sam_energy));
}

void UiComponents::styleSwitch(lv_obj_t * sw,
                               lv_color_t surface,
                               lv_color_t action) {
    if(!sw) return;
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(sw, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, surface, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, action, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_90, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sw, 0, LV_PART_KNOB);
}

void UiComponents::applyThemeTree(lv_obj_t * root,
                                  const UiTokens & previous,
                                  const UiTokens & next) {
    if(!root) return;
    const lv_style_selector_t selectors[] = {
        LV_PART_MAIN,
        LV_PART_MAIN | LV_STATE_CHECKED,
        LV_PART_MAIN | LV_STATE_PRESSED,
        LV_PART_INDICATOR,
        LV_PART_INDICATOR | LV_STATE_CHECKED,
        LV_PART_KNOB,
        LV_PART_ITEMS,
        LV_PART_ITEMS | LV_STATE_CHECKED,
        LV_PART_SCROLLBAR,
    };
    for(const lv_style_selector_t selector : selectors) {
        applySelector(root, selector, previous, next);
    }
    const uint32_t child_count = lv_obj_get_child_cnt(root);
    for(uint32_t i = 0; i < child_count; ++i) {
        applyThemeTree(lv_obj_get_child(root, i), previous, next);
    }
    lv_obj_invalidate(root);
}

}  // namespace firefly

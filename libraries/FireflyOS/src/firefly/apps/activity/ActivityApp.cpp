#include "ActivityApp.h"

#include <stdio.h>

#include "../../ui/UiTheme.h"

namespace firefly {

bool ActivityApp::create(lv_obj_t * parent, UiComponents & components) {
    LV_UNUSED(components);
    if(!parent) return false;
    const UiTokens tokens = UiTheme::fireflyDefault();
    root_ = UiComponents::createPage(parent, tokens);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * title = lv_label_create(root_);
    lv_label_set_text(title, "Today's Activity");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(tokens.text_primary), 0);
    lv_obj_set_pos(title, 28, 54);

    status_label_ = lv_label_create(root_);
    lv_obj_set_width(status_label_, 350);
    lv_obj_set_style_text_color(status_label_,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(status_label_, "QMI8658  |  Waiting for motion");
    lv_obj_set_pos(status_label_, 30, 90);

    goal_arc_ = lv_arc_create(root_);
    lv_obj_set_size(goal_arc_, 220, 220);
    lv_obj_align(goal_arc_, LV_ALIGN_TOP_MID, 0, 118);
    lv_arc_set_rotation(goal_arc_, 135);
    lv_arc_set_bg_angles(goal_arc_, 0, 270);
    lv_arc_set_range(goal_arc_, 0, static_cast<int32_t>(kDefaultGoalSteps));
    lv_arc_set_value(goal_arc_, 0);
    lv_obj_set_style_arc_width(goal_arc_, 18, LV_PART_MAIN);
    lv_obj_set_style_arc_color(goal_arc_, lv_color_hex(tokens.bg_surface),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_width(goal_arc_, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(goal_arc_,
                               lv_color_hex(tokens.firefly_primary),
                               LV_PART_INDICATOR);
    lv_obj_clear_flag(goal_arc_, LV_OBJ_FLAG_CLICKABLE);

    steps_label_ = lv_label_create(root_);
    lv_obj_set_width(steps_label_, 190);
    lv_obj_set_style_text_align(steps_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(steps_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(steps_label_,
                                lv_color_hex(tokens.text_primary), 0);
    lv_label_set_text(steps_label_, "0");
    lv_obj_align(steps_label_, LV_ALIGN_TOP_MID, 0, 190);

    lv_obj_t * steps_caption = lv_label_create(root_);
    lv_label_set_text(steps_caption, "STEPS");
    lv_obj_set_style_text_color(steps_caption,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_obj_align(steps_caption, LV_ALIGN_TOP_MID, 0, 250);

    lv_obj_t * active_card = UiComponents::createCard(root_, tokens);
    lv_obj_set_size(active_card, 168, 86);
    lv_obj_set_pos(active_card, 28, 356);
    lv_obj_t * active_title = lv_label_create(active_card);
    lv_label_set_text(active_title, "ACTIVE MINUTES");
    lv_obj_set_style_text_color(active_title,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_obj_set_pos(active_title, 12, 12);
    active_label_ = lv_label_create(active_card);
    lv_label_set_text(active_label_, "0 min");
    lv_obj_set_style_text_font(active_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(active_label_,
                                lv_color_hex(tokens.firefly_primary), 0);
    lv_obj_set_pos(active_label_, 12, 40);

    lv_obj_t * goal_card = UiComponents::createCard(root_, tokens);
    lv_obj_set_size(goal_card, 168, 86);
    lv_obj_set_pos(goal_card, 214, 356);
    lv_obj_t * goal_title = lv_label_create(goal_card);
    lv_label_set_text(goal_title, "DAILY GOAL");
    lv_obj_set_style_text_color(goal_title,
                                lv_color_hex(tokens.text_secondary), 0);
    lv_obj_set_pos(goal_title, 12, 12);
    goal_label_ = lv_label_create(goal_card);
    lv_label_set_text(goal_label_, "0%");
    lv_obj_set_style_text_font(goal_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(goal_label_,
                                lv_color_hex(tokens.sam_energy), 0);
    lv_obj_set_pos(goal_label_, 12, 40);
    return true;
}

void ActivityApp::destroy() {
    if(root_) lv_obj_del(root_);
    root_ = nullptr;
    status_label_ = nullptr;
    goal_arc_ = nullptr;
    steps_label_ = nullptr;
    active_label_ = nullptr;
    goal_label_ = nullptr;
}

void ActivityApp::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ActivityApp::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ActivityApp::refresh(const MotionSummary & summary) {
    if(!root_) return;
    if(!summary.sensor_available) {
        lv_label_set_text(status_label_, "Motion unavailable | Other apps work");
        lv_label_set_text(steps_label_, "--");
        lv_label_set_text(active_label_, "-- min");
        lv_label_set_text(goal_label_, "--");
        lv_arc_set_value(goal_arc_, 0);
        return;
    }

    lv_label_set_text(status_label_, "QMI8658 | Local estimate, non-medical");
    char text[24];
    snprintf(text, sizeof(text), "%lu",
             static_cast<unsigned long>(summary.steps));
    lv_label_set_text(steps_label_, text);
    snprintf(text, sizeof(text), "%u min",
             static_cast<unsigned>(summary.active_minutes));
    lv_label_set_text(active_label_, text);
    const uint32_t progress = summary.steps >= kDefaultGoalSteps
        ? 100U
        : (summary.steps * 100U) / kDefaultGoalSteps;
    snprintf(text, sizeof(text), "%lu%%", static_cast<unsigned long>(progress));
    lv_label_set_text(goal_label_, text);
    lv_arc_set_value(goal_arc_, static_cast<int32_t>(
        summary.steps > kDefaultGoalSteps ? kDefaultGoalSteps : summary.steps));
}

}  // namespace firefly

#include <lvgl/lvgl.h>
#include <math.h>
#include <stdio.h>

#include "gui.h"
#include "log.h"
#include "assets.h"
#include "decoder.h"
#include "drivers.h"
#include "apps.h"

enum {
    DialogButton_min,

    DialogButton_delete = DialogButton_min,
    DialogButton_load,
    DialogButton_star,
    DialogButton_back,

    DialogButton_max
};

static lv_img_dsc_t g_background;

static lv_ll_t g_apps_ll;
static int g_apps_ll_len;

static lv_img_dsc_t g_star_dscs[2]; // {small, big}

static lv_obj_t *g_curr_focused_tmp = NULL;

static lv_img_dsc_t g_list_dscs[2] = {0}; // {normal, hover}
static lv_obj_t *g_list_buttons[MAX_LIST_ROWS] = {0};
static lv_obj_t *g_list_buttons_tmp[MAX_LIST_ROWS] = {0};

static lv_obj_t *g_list_covers[MAX_LIST_ROWS] = {0}; // Needed because when objects are direct children of an imgbtn, they're always centered (don't know why)
static lv_obj_t *g_list_covers_tmp[MAX_LIST_ROWS] = {0};

static lv_img_dsc_t g_dialog_bg;
static lv_img_dsc_t g_dialog_buttons_dscs[2] = {0};
static lv_obj_t *g_dialog_buttons[DialogButton_max] = {0};
static lv_obj_t *g_dialog_cover = NULL;

static char *g_dialog_buttons_text[] = {"Delete", "Load", "Star", "Back"};

static int g_list_index = 0; // -3 for right arrow, -2 for left

static lv_img_dsc_t g_arrow_dscs[4] = {0}; // {next_normal, next_hover, prev_normal, prev_hover}
static lv_obj_t *g_arrow_buttons[2] = {0}; // {next, prev}

static int g_curr_page = 0;
static lv_anim_t g_page_list_anims[MAX_LIST_ROWS] = {0};
static lv_anim_t g_page_arrow_anims[2] = {0};
static bool g_page_list_anim_running = false;
static bool g_page_arrow_anim_running = false;

static lv_img_dsc_t g_logo;

static lv_style_t g_transp_style;
static lv_style_t g_dark_opa_64_style;
static lv_style_t g_white_48_style;
static lv_style_t g_white_28_style;
static lv_style_t g_white_16_style;

static void change_page(int dir);

static inline bool page_anim_running() {
    return g_page_list_anim_running || g_page_arrow_anim_running;
}

static inline int num_buttons() {
    return fmin(g_apps_ll_len - MAX_LIST_ROWS * g_curr_page, MAX_LIST_ROWS);
}

static app_entry_t *get_app_for_button(int btn_idx) {
    app_entry_t *entry;
    int i = 0;

    btn_idx = g_curr_page * MAX_LIST_ROWS + btn_idx;

    LV_LL_READ(g_apps_ll, entry) {
        if (i == btn_idx)
            return entry;
        i++;
    }

    return NULL;
}

static void focus_cb(lv_group_t *group, lv_style_t *style) { }

static void exit_dialog() {
    lv_obj_del(g_dialog_cover);

    for (int i = 0; i < num_buttons(); i++) {
        lv_group_add_obj(keypad_group(), g_list_buttons[i]);
        lv_event_send(g_list_buttons[i], LV_EVENT_DEFOCUSED, NULL);
    }
    
    for (int i = 0; i < 2; i++) {
        if (g_arrow_buttons[i] != NULL)
            lv_group_add_obj(keypad_group(), g_arrow_buttons[i]);
    }

    lv_group_focus_obj(g_curr_focused_tmp);
    g_curr_focused_tmp = NULL;

    for (int i = 0; i < DialogButton_max; i++)
        g_dialog_buttons[i] = NULL;
}

static void dialog_cover_event(lv_obj_t *obj, lv_event_t event) {
    // This will need to get taken out or changed so that wehen you click on dialog_bg this also doesn't get called
    if (event == LV_EVENT_CLICKED)
        exit_dialog();
}

static void dialog_button_event(lv_obj_t *obj, lv_event_t event) {
    switch (event) {
        case LV_EVENT_FOCUSED: {
            lv_imgbtn_set_src(obj, LV_BTN_STATE_REL, &g_dialog_buttons_dscs[1]);
            lv_imgbtn_set_src(obj, LV_BTN_STATE_PR, &g_dialog_buttons_dscs[1]);
        } break;

        case LV_EVENT_DEFOCUSED: {
            lv_imgbtn_set_src(obj, LV_BTN_STATE_REL, &g_dialog_buttons_dscs[0]);
            lv_imgbtn_set_src(obj, LV_BTN_STATE_PR, &g_dialog_buttons_dscs[0]);
        } break;
    }
}

static void draw_app_dialog() {
    g_curr_focused_tmp = g_list_buttons[g_list_index];
    lv_event_send(g_list_buttons[g_list_index], LV_EVENT_DEFOCUSED, NULL);
    lv_group_remove_all_objs(keypad_group());

    g_dialog_cover = lv_obj_create(lv_scr_act(), NULL);
    lv_obj_set_event_cb(g_dialog_cover, dialog_cover_event);
    lv_obj_set_style(g_dialog_cover, &g_dark_opa_64_style);
    lv_obj_set_size(g_dialog_cover, LV_HOR_RES_MAX, LV_VER_RES_MAX);

    lv_obj_t *dialog_bg = lv_img_create(g_dialog_cover, NULL);
    lv_img_set_src(dialog_bg, &g_dialog_bg);
    lv_obj_set_event_cb(dialog_bg, dialog_cover_event);

    lv_obj_align(dialog_bg, NULL, LV_ALIGN_CENTER, 0, 0);

    app_entry_t *entry = get_app_for_button(g_list_index);

    lv_obj_t *name = lv_label_create(dialog_bg, NULL);
    lv_obj_set_style(name, &g_white_48_style);
    lv_label_set_static_text(name, entry->name);
    lv_obj_align(name, NULL, LV_ALIGN_IN_TOP_MID, 0, 20);

    lv_obj_t *icon = lv_img_create(dialog_bg, NULL);
    lv_img_set_src(icon, &entry->icon);
    lv_obj_align(icon, NULL, LV_ALIGN_IN_TOP_LEFT, 40, 48 + 20 + 20);

    if (entry->starred) {
        lv_obj_t *star = lv_img_create(dialog_bg, NULL);
        lv_img_set_src(star, &g_star_dscs[1]);
        lv_obj_align(star, icon, LV_ALIGN_IN_TOP_LEFT, -STAR_BIG_W / 2, -STAR_BIG_H / 2);
    }

    char version_text[sizeof("Version: ") + APP_VER_LEN] = {0};
    sprintf(version_text, "Version: %s", entry->version);

    lv_obj_t *ver = lv_label_create(dialog_bg, NULL);
    lv_obj_set_style(ver, &g_white_28_style);
    lv_label_set_text(ver, version_text);
    lv_obj_align(ver, icon, LV_ALIGN_OUT_RIGHT_TOP, 20, 20);

    char author_text[sizeof("Author: ") + APP_AUTHOR_LEN] = {0};
    sprintf(author_text, "Author: %s", entry->author);

    lv_obj_t *auth = lv_label_create(dialog_bg, ver);
    lv_label_set_text(auth, author_text);
    lv_obj_align(auth, icon, LV_ALIGN_OUT_RIGHT_TOP, 20, 20 + 28 + 10);

    g_dialog_buttons[0] = lv_imgbtn_create(dialog_bg, NULL);
    lv_imgbtn_set_src(g_dialog_buttons[0], LV_BTN_STATE_REL, &g_dialog_buttons_dscs[0]);
    lv_imgbtn_set_src(g_dialog_buttons[0], LV_BTN_STATE_PR, &g_dialog_buttons_dscs[0]);
    lv_group_add_obj(keypad_group(), g_dialog_buttons[0]);
    lv_obj_set_event_cb(g_dialog_buttons[0], dialog_button_event);
    lv_obj_align(g_dialog_buttons[0], NULL, LV_ALIGN_IN_BOTTOM_LEFT, 40, -20);

    lv_obj_t *button_labels[DialogButton_max];

    button_labels[0] = lv_label_create(g_dialog_buttons[0], NULL);
    lv_obj_set_style(button_labels[0], &g_white_28_style);
    lv_label_set_static_text(button_labels[0], g_dialog_buttons_text[DialogButton_min]);

    for (int i = 1; i < DialogButton_max; i++) {
        g_dialog_buttons[i] = lv_imgbtn_create(dialog_bg, g_dialog_buttons[i - 1]);
        lv_obj_align(g_dialog_buttons[i], g_dialog_buttons[i - 1], LV_ALIGN_OUT_RIGHT_MID, 0, 0);

        button_labels[i] = lv_label_create(g_dialog_buttons[i], button_labels[i - 1]);
        lv_label_set_static_text(button_labels[i], g_dialog_buttons_text[i]);
    }

    lv_group_focus_obj(g_dialog_buttons[DialogButton_load]);

    //lv_group_focus_freeze(keypad_group(), true);
}

static void list_button_event(lv_obj_t *obj, lv_event_t event) {
    if (keypad_group()->frozen)
        return;

    // Covers also use this event so make sure the object we use is the button
    for (int i = 0; i < MAX_LIST_ROWS; i++) {
        if (obj == g_list_covers[i]) {
            obj = g_list_buttons[i];
            break;
        }
    }

    switch (event) {
        case LV_EVENT_FOCUSED: {
            lv_imgbtn_set_src(obj, LV_BTN_STATE_REL, &g_list_dscs[1]);
            lv_imgbtn_set_src(obj, LV_BTN_STATE_PR, &g_list_dscs[1]);

            for (g_list_index = 0; g_list_index < MAX_LIST_ROWS; g_list_index++) {
                if (obj == g_list_buttons[g_list_index])
                    break;
            }
        } break;

        case LV_EVENT_DEFOCUSED: {
            lv_imgbtn_set_src(obj, LV_BTN_STATE_REL, &g_list_dscs[0]);
            lv_imgbtn_set_src(obj, LV_BTN_STATE_PR, &g_list_dscs[0]);
        } break;

        case LV_EVENT_KEY: {
            const u32 *key = lv_event_get_data();

            switch (*key) {
                case LV_KEY_DOWN:
                    g_list_index += 1;
                    break;
                case LV_KEY_UP:
                    g_list_index -= 1;
                    break;
                case LV_KEY_RIGHT:
                    lv_group_focus_obj(g_arrow_buttons[0]);
                    return;
                case LV_KEY_LEFT:
                    lv_group_focus_obj(g_arrow_buttons[1]);
                    return;
            }

            if (g_list_index >= num_buttons())
                g_list_index = 0;
            else if (g_list_index < 0)
                g_list_index = num_buttons() - 1;

            lv_group_focus_obj(g_list_buttons[g_list_index]);
        } break;

        case LV_EVENT_CLICKED: {
            lv_group_focus_obj(obj);
            draw_app_dialog();
        } break;
    }
}

static void arrow_button_event(lv_obj_t *obj, lv_event_t event) {
    if (keypad_group()->frozen)
        return;
    if (page_anim_running() && (event != LV_EVENT_FOCUSED && event != LV_EVENT_DEFOCUSED))
        return;

    switch (event) {
        case LV_EVENT_FOCUSED: {
            if (obj == g_arrow_buttons[0]) {
                lv_imgbtn_set_src(obj, LV_BTN_STATE_REL, &g_arrow_dscs[1]);
                lv_imgbtn_set_src(obj, LV_BTN_STATE_PR, &g_arrow_dscs[1]);

                g_list_index = -3;
            } else {
                lv_imgbtn_set_src(obj, LV_BTN_STATE_REL, &g_arrow_dscs[3]);
                lv_imgbtn_set_src(obj, LV_BTN_STATE_PR, &g_arrow_dscs[3]);

                g_list_index = -2;
            }
        } break;

        case LV_EVENT_DEFOCUSED: {
            if (obj == g_arrow_buttons[0]) {
                lv_imgbtn_set_src(obj, LV_BTN_STATE_REL, &g_arrow_dscs[0]);
                lv_imgbtn_set_src(obj, LV_BTN_STATE_PR, &g_arrow_dscs[0]);
            } else {
                lv_imgbtn_set_src(obj, LV_BTN_STATE_REL, &g_arrow_dscs[2]);
                lv_imgbtn_set_src(obj, LV_BTN_STATE_PR, &g_arrow_dscs[2]);
            }
        } break;

        case LV_EVENT_KEY: {
            const u32 *key = lv_event_get_data();

            switch (*key) {
                case LV_KEY_LEFT:
                    if (obj == g_arrow_buttons[0]) 
                        lv_group_focus_obj(g_list_buttons[num_buttons() / 2]);

                    break;
                case LV_KEY_RIGHT:
                    if (obj == g_arrow_buttons[1])
                        lv_group_focus_obj(g_list_buttons[num_buttons() / 2]);

                    break;
            }
        } break;

        case LV_EVENT_CLICKED: {
            if (obj == g_arrow_buttons[0])
                change_page(1);
            else
                change_page(-1);
        } break;
    }
}

static void gen_apps_list() {
    app_entry_ll_init(&g_apps_ll);
    g_apps_ll_len = lv_ll_get_len(&g_apps_ll);
}

static void draw_entry_on_obj(lv_obj_t *obj, app_entry_t *entry) {
    u8 offset = (LIST_BTN_H - APP_ICON_SMALL_H) / 2;

    lv_obj_t *icon_small = lv_img_create(obj, NULL);
    lv_img_set_src(icon_small, &entry->icon_small);
    lv_obj_align(icon_small, NULL, LV_ALIGN_IN_LEFT_MID, offset, 0);

    if (entry->starred) {
        lv_obj_t *star = lv_img_create(obj, NULL);
        lv_img_set_src(star, &g_star_dscs[0]);
        lv_obj_align(star, icon_small, LV_ALIGN_IN_TOP_LEFT, -STAR_SMALL_W / 2, -STAR_SMALL_H / 2);
    }

    lv_obj_t *name = lv_label_create(obj, NULL);
    lv_label_set_style(name, LV_LABEL_STYLE_MAIN, &g_white_28_style);
    lv_label_set_static_text(name, entry->name);
    lv_label_set_align(name, LV_LABEL_ALIGN_LEFT);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CROP);
    lv_obj_align(name, icon_small, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *author = lv_label_create(obj, NULL);
    lv_label_set_style(author, LV_LABEL_STYLE_MAIN, &g_white_16_style);
    lv_label_set_align(author, LV_LABEL_ALIGN_RIGHT);
    lv_label_set_static_text(author, entry->author);
    lv_obj_align(author, NULL, LV_ALIGN_IN_BOTTOM_RIGHT, -offset, -offset);

    lv_obj_t *ver = lv_label_create(obj, author);
    lv_label_set_static_text(ver, entry->version);
    lv_obj_align(ver, NULL, LV_ALIGN_IN_TOP_RIGHT, -offset, offset);
}

static void draw_arrow_button(int idx) {
    g_arrow_buttons[idx] = lv_imgbtn_create(lv_scr_act(), NULL);
    lv_group_add_obj(keypad_group(), g_arrow_buttons[idx]);
    lv_obj_set_event_cb(g_arrow_buttons[idx], arrow_button_event);
    lv_imgbtn_set_src(g_arrow_buttons[idx], LV_BTN_STATE_REL, &g_arrow_dscs[idx * 2]);
    lv_imgbtn_set_src(g_arrow_buttons[idx], LV_BTN_STATE_PR, &g_arrow_dscs[idx * 2]);
    lv_obj_align(g_arrow_buttons[idx], NULL, LV_ALIGN_CENTER, ((idx == 0) ? 1 : -1) * ARROW_OFF, 0);
}

static void list_ready_cb(lv_anim_t *anim) {
    lv_obj_t *anim_obj = anim->var;
    int dir = (anim->start < anim->end) ? -1 : 1;

    int anim_idx = (lv_obj_get_y(anim_obj) - (LV_VER_RES_MAX - LIST_BTN_H * MAX_LIST_ROWS) / 2) / LIST_BTN_H;

    app_entry_t *entry = get_app_for_button(((dir < 0) ? MAX_LIST_ROWS : -MAX_LIST_ROWS) + anim_idx);

    if (entry != NULL)
        app_entry_free_icon(entry);

    if (g_list_buttons_tmp[anim_idx] != NULL) {
        lv_obj_set_parent(g_list_buttons_tmp[anim_idx], lv_scr_act());
        lv_obj_align(g_list_buttons_tmp[anim_idx], anim_obj, (dir < 0) ? LV_ALIGN_IN_LEFT_MID : LV_ALIGN_IN_RIGHT_MID, 0, 0);
    }

    lv_anim_del(anim_obj, anim->exec_cb);
    lv_obj_del(anim_obj);

    if (anim_idx == MAX_LIST_ROWS - 1)
        g_page_list_anim_running = false;

    g_list_buttons[anim_idx] = g_list_buttons_tmp[anim_idx];
    g_list_covers[anim_idx] = g_list_covers_tmp[anim_idx];

    g_list_buttons_tmp[anim_idx] = NULL;
    g_list_covers_tmp[anim_idx] = NULL;
}

static void arrow_ready_cb(lv_anim_t *anim) {
    lv_obj_t *obj = anim->var;

    int idx;
    if (obj == g_arrow_buttons[0])
        idx = 0;
    else
        idx = 1;

    lv_anim_del(anim->var, anim->exec_cb);

    if (idx == 1)
        g_page_arrow_anim_running = false;

    if ((idx == 0 && num_buttons() < MAX_LIST_ROWS) || (idx == 1 && g_curr_page == 0)) {
        lv_obj_del(g_arrow_buttons[idx]);
        g_arrow_buttons[idx] = NULL;
        lv_group_focus_obj(g_list_buttons[0]);
    }

    lv_anim_clear_playback(&g_page_arrow_anims[idx]);
}

static void change_page(int dir) {
    g_page_list_anim_running = true;
    g_page_arrow_anim_running = true;

    lv_obj_t *anim_objs[MAX_LIST_ROWS];

    anim_objs[0] = lv_obj_create(lv_scr_act(), NULL);
    lv_obj_set_style(anim_objs[0], &g_transp_style);
    lv_obj_set_size(anim_objs[0], LV_HOR_RES_MAX + LIST_BTN_W, LIST_BTN_H);
    lv_obj_set_pos(anim_objs[0], ((dir < 0) ? -LV_HOR_RES_MAX : 0) + lv_obj_get_x(g_list_buttons[0]), lv_obj_get_y(g_list_buttons[0]));
    lv_obj_set_parent(g_list_buttons[0], anim_objs[0]);
    lv_obj_align(g_list_buttons[0], NULL, (dir < 0) ? LV_ALIGN_IN_RIGHT_MID : LV_ALIGN_IN_LEFT_MID, 0, 0);

    lv_imgbtn_set_src(g_list_buttons[0], LV_BTN_STATE_REL, &g_list_dscs[0]);
    lv_imgbtn_set_src(g_list_buttons[0], LV_BTN_STATE_PR, &g_list_dscs[0]);

    for (int i = 1; i < MAX_LIST_ROWS; i++) {
        anim_objs[i] = lv_obj_create(lv_scr_act(), anim_objs[i - 1]);
        lv_obj_align(anim_objs[i], anim_objs[i - 1], LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

        if (g_list_buttons[i] != NULL) {
            lv_obj_set_parent(g_list_buttons[i], anim_objs[i]);
            lv_obj_align(g_list_buttons[i], NULL, (dir < 0) ? LV_ALIGN_IN_RIGHT_MID : LV_ALIGN_IN_LEFT_MID, 0, 0);
        }
    }

    app_entry_t *entry = get_app_for_button(((dir < 0) ? -1 : 1) * MAX_LIST_ROWS);
    g_curr_page += dir;
    for (int i = 0; i < num_buttons(); i++) {
        g_list_buttons_tmp[i] = lv_imgbtn_create(anim_objs[i], g_list_buttons[0]);
        g_list_covers_tmp[i] = lv_obj_create(g_list_buttons_tmp[i], g_list_covers[0]);

        if (i > 0)
            entry = lv_ll_get_next(&g_apps_ll, entry);

        app_entry_init_icon(entry);
        draw_entry_on_obj(g_list_covers_tmp[i], entry);

        lv_obj_align(g_list_buttons_tmp[i], anim_objs[i], (dir < 0) ? LV_ALIGN_IN_LEFT_MID : LV_ALIGN_IN_RIGHT_MID, 0, 0);
    }

    for (int i = 0; i < MAX_LIST_ROWS; i++) {
        lv_anim_set_exec_cb(&g_page_list_anims[i], anim_objs[i], (lv_anim_exec_xcb_t) lv_obj_set_x);
        lv_anim_set_values(&g_page_list_anims[i], lv_obj_get_x(anim_objs[i]), lv_obj_get_x(anim_objs[i]) + ((dir < 0) ? 1 : -1) * LV_HOR_RES_MAX);

        lv_anim_create(&g_page_list_anims[i]);
    }

    for (int i = 0; i < 2; i++) {
        if (g_arrow_buttons[i] != NULL) {
            lv_anim_set_values(&g_page_arrow_anims[i], lv_obj_get_x(g_arrow_buttons[i]), (i == 0) ? LV_HOR_RES_MAX : -ARROW_BTN_W);
            lv_anim_set_time(&g_page_arrow_anims[i], (PAGE_WAIT * (MAX_LIST_ROWS - 1) + PAGE_TIME) / 2, 0);

            if ((i == 0 && num_buttons() >= MAX_LIST_ROWS) || (i == 1 && g_curr_page != 0))
                lv_anim_set_playback(&g_page_arrow_anims[i], 0);
        } else {
            draw_arrow_button(i);
            lv_obj_set_x(g_arrow_buttons[i], (i == 0) ? LV_HOR_RES_MAX : -ARROW_BTN_W);
            
            lv_anim_set_values(&g_page_arrow_anims[i], lv_obj_get_x(g_arrow_buttons[i]), (LV_HOR_RES_MAX - ARROW_BTN_W) / 2 + ((i == 0) ? 1 : -1) * ARROW_OFF);
            lv_anim_set_time(&g_page_arrow_anims[i], (PAGE_WAIT * (MAX_LIST_ROWS - 1) + PAGE_TIME) / 2, (PAGE_WAIT * (MAX_LIST_ROWS - 1) + PAGE_TIME) / 2);
        }
        
        lv_anim_set_exec_cb(&g_page_arrow_anims[i], g_arrow_buttons[i], (lv_anim_exec_xcb_t) lv_obj_set_x);
        
        lv_anim_create(&g_page_arrow_anims[i]);
    }
}

static void draw_buttons() {
    if (num_buttons() > 0) {
        lv_group_set_style_mod_cb(keypad_group(), focus_cb);

        g_list_buttons[0] = lv_imgbtn_create(lv_scr_act(), NULL);
        lv_group_add_obj(keypad_group(), g_list_buttons[0]);
        lv_obj_set_event_cb(g_list_buttons[0], list_button_event);
        lv_imgbtn_set_src(g_list_buttons[0], LV_BTN_STATE_REL, &g_list_dscs[0]);
        lv_imgbtn_set_src(g_list_buttons[0], LV_BTN_STATE_PR, &g_list_dscs[0]);
        lv_obj_align(g_list_buttons[0], NULL, LV_ALIGN_IN_TOP_MID, 0, (LV_VER_RES_MAX - LIST_BTN_H * MAX_LIST_ROWS) / 2);

        g_list_covers[0] = lv_obj_create(g_list_buttons[0], NULL);
        lv_obj_set_event_cb(g_list_covers[0], list_button_event);
        lv_obj_set_style(g_list_covers[0], &g_transp_style);
        lv_obj_set_size(g_list_covers[0], lv_obj_get_width(g_list_buttons[0]), lv_obj_get_height(g_list_buttons[0]));

        app_entry_t *entry = get_app_for_button(0);
        app_entry_init_icon(entry);
        draw_entry_on_obj(g_list_covers[0], entry);

        for (int i = 1; i < num_buttons(); i++) {
            g_list_buttons[i] = lv_imgbtn_create(lv_scr_act(), g_list_buttons[i - 1]);
            g_list_covers[i] = lv_obj_create(g_list_buttons[i], g_list_covers[i - 1]);

            entry = lv_ll_get_next(&g_apps_ll, entry);
            app_entry_init_icon(entry);
            draw_entry_on_obj(g_list_covers[i], entry);

            lv_obj_align(g_list_buttons[i], g_list_buttons[i - 1], LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
        }

        lv_event_send(g_list_buttons[0], LV_EVENT_FOCUSED, NULL);

        if (num_buttons() >= MAX_LIST_ROWS) {
            draw_arrow_button(0);
        }

        if (g_curr_page > 0) {
            draw_arrow_button(1);
        }

        for (int i = 0; i < MAX_LIST_ROWS; i++) {
            lv_anim_set_time(&g_page_list_anims[i], PAGE_TIME, PAGE_WAIT * i);
            lv_anim_set_path_cb(&g_page_list_anims[i], lv_anim_path_linear);
            lv_anim_set_ready_cb(&g_page_list_anims[i], list_ready_cb);
        }

        for (int i = 0; i < 2; i++) {
            lv_anim_set_path_cb(&g_page_arrow_anims[i], lv_anim_path_linear);
            lv_anim_set_ready_cb(&g_page_arrow_anims[i], arrow_ready_cb);
        }
    }
}

void setup_screen() {
    u8 *data;
    size_t size;

    assetsGetData(AssetId_background, &data, &size);
    g_background = (lv_img_dsc_t) {
        .header.always_zero = 0,
        .header.w = LV_HOR_RES_MAX,
        .header.h = LV_VER_RES_MAX,
        .data_size = size,
        .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .data = data,
    };

    lv_obj_t *scr = lv_img_create(NULL, NULL);
    lv_img_set_src(scr, &g_background);
    lv_scr_load(scr);
}

void setup_menu() {
    lv_style_copy(&g_transp_style, &lv_style_plain);
    g_transp_style.body.opa = LV_OPA_TRANSP;

    lv_style_copy(&g_dark_opa_64_style, &lv_style_plain);
    g_dark_opa_64_style.body.main_color = LV_COLOR_BLACK;
    g_dark_opa_64_style.body.grad_color = LV_COLOR_BLACK;
    g_dark_opa_64_style.body.opa = 64;

    lv_style_copy(&g_white_48_style, &lv_style_plain);
    g_white_48_style.text.font = &lv_font_roboto_48;
    g_white_48_style.text.color = LV_COLOR_WHITE;

    lv_style_copy(&g_white_28_style, &lv_style_plain);
    g_white_28_style.text.font = &lv_font_roboto_28;
    g_white_28_style.text.color = LV_COLOR_WHITE;

    lv_style_copy(&g_white_16_style, &lv_style_plain);
    g_white_16_style.text.color = LV_COLOR_WHITE;

    u8 *data;
    size_t size;

    // List buttons
    for (int i = 0; i < 2; i++) {
        assetsGetData(AssetId_apps_list + i, &data, &size);
        g_list_dscs[i] = (lv_img_dsc_t) {
            .header.always_zero = 0,
            .header.w = LIST_BTN_W,
            .header.h = LIST_BTN_H,
            .data_size = size,
            .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
            .data = data,
        };
    }

    // Arrow buttons
    for (int i = 0; i < 4; i++) {
        assetsGetData(AssetId_apps_next + i, &data, &size);
        g_arrow_dscs[i] = (lv_img_dsc_t) {
            .header.always_zero = 0,
            .header.w = ARROW_BTN_W,
            .header.h = ARROW_BTN_H,
            .data_size = size,
            .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
            .data = data,
        };
    }

    assetsGetData(AssetId_star_small, &data, &size);
    g_star_dscs[0] = (lv_img_dsc_t) {
        .header.always_zero = 0,
        .header.w = STAR_SMALL_W,
        .header.h = STAR_SMALL_H,
        .data_size = size,
        .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .data = data,
    };

    assetsGetData(AssetId_star_big, &data, &size);
    g_star_dscs[1] = (lv_img_dsc_t) {
        .header.always_zero = 0,
        .header.w = STAR_BIG_W,
        .header.h = STAR_BIG_H,
        .data_size = size,
        .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .data = data,
    };

    assetsGetData(AssetId_dialog_background, &data, &size);
    g_dialog_bg = (lv_img_dsc_t) {
        .header.always_zero = 0,
        .header.w = DIALOG_BG_W,
        .header.h = DIALOG_BG_H,
        .data_size = size,
        .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .data = data,
    };

    for (int i = 0; i < 2; i++) {
        assetsGetData(AssetId_button_tiny + i, &data, &size);
        g_dialog_buttons_dscs[i] = (lv_img_dsc_t) {
            .header.always_zero = 0,
            .header.w = DIALOG_BTN_W,
            .header.h = DIALOG_BTN_H,
            .data_size = size,
            .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
            .data = data,
        };
    }

    gen_apps_list();
    draw_buttons();
}

void setup_misc() {
    u8 *data;
    size_t size;

    assetsGetData(AssetId_logo, &data, &size);
    g_logo = (lv_img_dsc_t) {
        .header.always_zero = 0,
        .header.w = LOGO_W,
        .header.h = LOGO_H,
        .data_size = size,
        .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .data = data,
    };

    lv_obj_t *logo = lv_img_create(lv_scr_act(), NULL);
    lv_img_set_src(logo, &g_logo);
    lv_obj_align(logo, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 10, -32);
}
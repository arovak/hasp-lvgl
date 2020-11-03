/*********************
 *      INCLUDES
 *********************/
#include "hasp_conf.h"
#include <Arduino.h>
#include "ArduinoLog.h"
#include "ArduinoJson.h"
#include "StreamUtils.h"

#include "lvgl.h"
#include "lv_conf.h"
#include "hasp_conf.h"

//#include "../lib/lvgl/src/lv_widgets/lv_roller.h"

#include "lv_fs_if.h"
#include "hasp_debug.h"
#include "hasp_config.h"
#include "hasp_dispatch.h"
#include "hasp_wifi.h"
#include "hasp_gui.h"
#include "hasp_tft.h"

//#include "hasp_attr_get.h"
#include "hasp_attribute.h"
#include "hasp.h"
#include "lv_theme_hasp.h"

#include "EEPROM.h"

//#if LV_USE_HASP

/*********************
 *      DEFINES
 *********************/
uint8_t haspStartDim   = 100;
uint8_t haspStartPage  = 0;
uint8_t haspThemeId    = 0;
uint16_t haspThemeHue  = 200;
char haspPagesPath[32] = "/pages.jsonl";
char haspZiFontPath[32];

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
// void hasp_background(uint16_t pageid, uint16_t imageid);

#if LV_USE_ANIMATION
// static void kb_hide_anim_end(lv_anim_t * a);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
lv_style_t style_mbox_bg; /*Black bg. style with opacity*/
lv_obj_t * kb;
// lv_font_t * defaultFont;

#if LV_DEMO_WALLPAPER
LV_IMG_DECLARE(img_bubble_pattern)
#endif

/*
LV_IMG_DECLARE(xmass)

LV_IMG_DECLARE(frame00)
LV_IMG_DECLARE(frame02)
LV_IMG_DECLARE(frame04)
LV_IMG_DECLARE(frame06)
LV_IMG_DECLARE(frame08)
LV_IMG_DECLARE(frame10)
LV_IMG_DECLARE(frame12)
LV_IMG_DECLARE(frame14)
*/

/*
static const char * btnm_map1[] = {" ", "\n", " ", "\n", " ", "\n", " ", "\n", "P1", "P2", "P3", ""};

static const char * btnm_map2[] = {"0",  "1", "\n", "2",  "3",  "\n", "4",  "5",
                                   "\n", "6", "7",  "\n", "P1", "P2", "P3", ""};
*/

lv_obj_t * pages[HASP_NUM_PAGES];
static lv_font_t * haspFonts[4] = {nullptr, LV_THEME_DEFAULT_FONT_NORMAL, LV_THEME_DEFAULT_FONT_SUBTITLE,
                                   LV_THEME_DEFAULT_FONT_TITLE};
uint8_t current_page            = 0;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void haspLoadPage(const char * pages);

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Get Font ID
 */
lv_font_t * hasp_get_font(uint8_t fontid)
{
    if(fontid >= 4) {
        return nullptr;
    } else {
        return haspFonts[fontid];
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Get Page Object by PageID
 */
lv_obj_t * get_page(uint8_t pageid)
{
    if(pageid == 254) return lv_layer_top();
    if(pageid == 255) return lv_layer_sys();
    if(pageid >= sizeof pages / sizeof *pages) return NULL;
    return pages[pageid];
}
bool get_page_id(lv_obj_t * obj, uint8_t * pageid)
{
    lv_obj_t * page = lv_obj_get_screen(obj);

    if(!page) return false;

    if(page == lv_layer_top()) {
        *pageid = 254;
        return true;
    }
    if(page == lv_layer_sys()) {
        *pageid = 255;
        return true;
    }

    for(uint8_t i = 0; i < sizeof pages / sizeof *pages; i++) {
        if(page == pages[i]) {
            *pageid = i;
            return true;
        }
    }
    return false;
}

lv_obj_t * hasp_find_obj_from_id(lv_obj_t * parent, uint8_t objid)
{
    if(objid == 0) return parent;
    lv_obj_t * child;
    child = lv_obj_get_child(parent, NULL);
    while(child) {
        if(child->user_data && (lv_obj_user_data_t)objid == child->user_data) return child; // object found

        /* check grandchildren */
        lv_obj_t * grandchild = hasp_find_obj_from_id(child, objid);
        if(grandchild) return grandchild;

        /* check tabs */
        if(check_obj_type(child, LV_HASP_TABVIEW)) {
            uint16_t tabcount = lv_tabview_get_tab_count(child);
            for(uint16_t i = 0; i < tabcount; i++) {
                lv_obj_t * tab = lv_tabview_get_tab(child, i);
                Log.verbose("Found tab %i", i);
                if(tab->user_data && (lv_obj_user_data_t)objid == tab->user_data) return tab; // object found

                grandchild = hasp_find_obj_from_id(tab, objid);
                if(grandchild) return grandchild;
            }
        }

        /* next sibling */
        child = lv_obj_get_child(parent, child);
    }
    return NULL;
}
lv_obj_t * hasp_find_obj_from_id(uint8_t pageid, uint8_t objid)
{
    return hasp_find_obj_from_id(get_page(pageid), objid);
}

bool FindIdFromObj(lv_obj_t * obj, uint8_t * pageid, lv_obj_user_data_t * objid)
{
    if(!get_page_id(obj, pageid)) return false;
    if(!(obj->user_data > 0)) return false;
    memcpy(objid, &obj->user_data, sizeof(lv_obj_user_data_t));
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void hasp_send_obj_attribute_str(lv_obj_t * obj, const char * attribute, const char * data)
{
    uint8_t pageid;
    uint8_t objid;

    if(FindIdFromObj(obj, &pageid, &objid)) {
        dispatch_obj_attribute_str(pageid, objid, attribute, data);
    }
}

void hasp_send_obj_attribute_int(lv_obj_t * obj, const char * attribute, int32_t val)
{
    char data[64];
    itoa(val, data, 10);
    hasp_send_obj_attribute_str(obj, attribute, data);
}

void hasp_send_obj_attribute_color(lv_obj_t * obj, const char * attribute, lv_color_t color)
{
    char buffer[128];
    lv_color32_t c32;
    c32.full = lv_color_to32(color);
    snprintf(buffer, sizeof(buffer), PSTR("#%02x%02x%02x"), c32.ch.red, c32.ch.green, c32.ch.blue);
    hasp_send_obj_attribute_str(obj, attribute, buffer);
}

/** Senders for event handlers **/

static void hasp_send_obj_attribute_P(lv_obj_t * obj, const char * attr, const char * data)
{
    char * buffer;
    buffer = (char *)malloc(strlen_P(attr) + 1);
    strcpy_P(buffer, attr);
    hasp_send_obj_attribute_str(obj, buffer, data);
    free(buffer);
}

static inline void hasp_send_obj_attribute_val(lv_obj_t * obj, int32_t val)
{
    char data[64];
    itoa(val, data, 10);
    hasp_send_obj_attribute_P(obj, PSTR("val"), data);
}

/*static inline void hasp_send_obj_attribute_val(lv_obj_t * obj, int16_t val)
{
    char data[64];
    itoa(val, data, 10);
    hasp_send_obj_attribute_P(obj, PSTR("val"), data);
}*/

static inline void hasp_send_obj_attribute_event(lv_obj_t * obj, const char * event)
{
    hasp_send_obj_attribute_P(obj, PSTR("event"), event);
}

static inline void hasp_send_obj_attribute_txt(lv_obj_t * obj, const char * txt)
{
    hasp_send_obj_attribute_P(obj, PSTR("txt"), txt);
}

/*static void hasp_send_obj_attribute_txt(lv_obj_t * obj, String & txt)
{
    hasp_send_obj_attribute_P(obj, PSTR("txt"), txt.c_str());
}*/

////////////////////////////////////////////////////////////////////////////////////////////////////

// Used in the dispatcher
void hasp_process_attribute(uint8_t pageid, uint8_t objid, const char * attr, const char * payload)
{
    hasp_process_obj_attribute(hasp_find_obj_from_id(pageid, objid), attr, payload, strlen(payload) > 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * WakeUp the display using a command instead of touch
 */
void haspWakeUp()
{
    lv_disp_trig_activity(NULL);
}

void haspDisconnect()
{

    /* Create a dark plain style for a message box's background (modal)*/
    // lv_style_copy(&style_mbox_bg, &lv_style_plain);
    // style_mbox_bg.body.main_color = LV_COLOR_BLACK;
    // style_mbox_bg.body.grad_color = LV_COLOR_BLACK;
    // style_mbox_bg.body.opa        = LV_OPA_60;

    // lv_obj_set_style(lv_disp_get_layer_sys(NULL), &style_mbox_bg);

    /*
        lv_obj_set_click(lv_disp_get_layer_sys(NULL), true);
        lv_obj_set_event_cb(lv_disp_get_layer_sys(NULL), NULL);
        lv_obj_set_user_data(lv_disp_get_layer_sys(NULL), 255);
        */

    /*
        lv_obj_t * obj = lv_obj_get_child(lv_disp_get_layer_sys(NULL), NULL);
        lv_obj_set_hidden(obj, false);
        obj = lv_obj_get_child(lv_disp_get_layer_sys(NULL), obj);
        lv_obj_set_hidden(obj, false);*/
}

void haspReconnect()
{
    /*Revert the top layer to not block*/
    // lv_obj_set_style(lv_disp_get_layer_sys(NULL), &lv_style_transp);
    // lv_obj_set_click(lv_disp_get_layer_sys(NULL), false);
    // lv_obj_set_event_cb(lv_disp_get_layer_sys(NULL), btn_event_handler);
    /*
        lv_obj_t * obj = lv_obj_get_child(lv_disp_get_layer_sys(NULL), NULL);
        lv_obj_set_hidden(obj, true);
        obj = lv_obj_get_child(lv_disp_get_layer_sys(NULL), obj);
        lv_obj_set_hidden(obj, true);*/
}

/*Add a custom apply callback*/
static void custom_font_apply_cb(lv_theme_t * th, lv_obj_t * obj, lv_theme_style_t name)
{
    lv_style_list_t * list;

    switch(name) {
        case LV_THEME_BTN:
            list = lv_obj_get_style_list(obj, LV_BTN_PART_MAIN);
            // _lv_style_list_add_style(list, &my_style);
            break;
    }
}

/**
 * Create a demo application
 */
void haspSetup()
{
    guiSetDim(haspStartDim);

    /******* File System Test ********************************************************************/
    lv_fs_file_t f;
    lv_fs_res_t res;
    res = lv_fs_open(&f, "F:/pages.jsonl", LV_FS_MODE_RD);
    if(res == LV_FS_RES_OK)
        Log.error(F("Opening pages.json OK"));
    else
        Log.verbose(F("Opening pages.json from FS failed %d"), res);

    uint32_t btoread = 128;
    uint32_t bread   = 0;
    char buffer[128];

    res = lv_fs_read(&f, &buffer, btoread, &bread);
    if(res == LV_FS_RES_OK) {
        Log.error(F("Reading pages.json OK %u"), bread);
        buffer[127] = '\0';
        Log.verbose(buffer);
    } else
        Log.verbose(F("Reading pages.json from FS failed %d"), res);

    res = lv_fs_close(&f);
    if(res == LV_FS_RES_OK)
        Log.error(F("Closing pages.json OK"));
    else
        Log.verbose(F("Closing pages.json on FS failed %d"), res);
        /******* File System Test ********************************************************************/

        /* ********** Font Initializations ********** */

#if HASP_USE_SPIFFS > 0
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    lv_zifont_init();

    if(lv_zifont_font_init(&haspFonts[0], haspZiFontPath, 32) != 0) {
        Log.error(F("HASP: Failed to set font to %s"), haspZiFontPath);
        haspFonts[0] = LV_FONT_DEFAULT;
    } else {
        // defaultFont = haspFonts[0];
    }
#endif
#endif

    /* ********** Font Initializations ********** */

    /* ********** Theme Initializations ********** */
    lv_theme_t * th;
    switch(haspThemeId) {
#if LV_USE_THEME_ALIEN == 1
        case 1:
            th = lv_theme_alien_init(haspThemeHue, defaultFont);
            break;
#endif
#if LV_USE_THEME_NIGHT == 1
        case 2:
            th = lv_theme_night_init(haspThemeHue, defaultFont); // heavy
            break;
#endif
#if(LV_USE_THEME_MONO == 1) || (LV_USE_THEME_EMPTY == 1)
        case 3:
            th = lv_theme_mono_init(LV_COLOR_PURPLE, LV_COLOR_BLACK, LV_THEME_DEFAULT_FLAGS, haspFonts[0], haspFonts[1],
                                    haspFonts[2], haspFonts[3]);
            break;
#endif
#if LV_USE_THEME_MATERIAL == 1
        case 4:
            th = lv_theme_material_init(LV_COLOR_PURPLE, LV_COLOR_ORANGE,
                                        LV_THEME_MATERIAL_FLAG_LIGHT + LV_THEME_MATERIAL_FLAG_NO_FOCUS, haspFonts[0],
                                        haspFonts[1], haspFonts[2], haspFonts[3]);
            break;
        case 9:
            th = lv_theme_material_init(LV_COLOR_PURPLE, LV_COLOR_ORANGE,
                                        LV_THEME_MATERIAL_FLAG_DARK + LV_THEME_MATERIAL_FLAG_NO_FOCUS, haspFonts[0],
                                        haspFonts[1], haspFonts[2], haspFonts[3]);
            break;
#endif

#if LV_USE_THEME_ZEN == 1
        case 5:
            th = lv_theme_zen_init(haspThemeHue, defaultFont); // lightweight break;
#endif
#if LV_USE_THEME_NEMO == 1
        case 6:
            th =
                // lv_theme_nemo_init(haspThemeHue, defaultFont); // heavy
                break;
#endif
#if LV_USE_THEME_TEMPL == 1
        case 7:
            th = lv_theme_templ_init(haspThemeHue, defaultFont); // lightweight, not for production...
            break;
#endif
#if(LV_USE_THEME_HASP == 1)
        case 8:
            th = lv_theme_hasp_init(lv_color_hsv_to_rgb(haspThemeHue, 100, 100),
                                    lv_color_hsv_to_rgb(haspThemeHue, 100, 100), LV_THEME_DEFAULT_FLAGS, haspFonts[0],
                                    haspFonts[1], haspFonts[2], haspFonts[3]);
            break;
#endif
        /*        case 0:
        #if LV_USE_THEME_DEFAULT == 1
                    th = lv_theme_default_init(haspThemeHue, defaultFont);
        #else
                    th = lv_theme_hasp_init(512, defaultFont);
        #endif
                    break;
        */
        default:
            th = lv_theme_template_init(LV_COLOR_PURPLE, LV_COLOR_ORANGE, LV_THEME_DEFAULT_FLAGS, haspFonts[0],
                                        haspFonts[1], haspFonts[2], haspFonts[3]);
            Log.error(F("HASP: Unknown theme selected"));
    }

    if(th) {
        Log.verbose(F("HASP: Custom theme loaded"));
    } else {
        Log.error(F("HASP: No theme could be loaded"));
    }
    // lv_theme_set_current(th);
    /* ********** Theme Initializations ********** */

    // lv_style_list_t * list;
    // static lv_style_t pagefont;
    // lv_style_init(&pagefont);
    // lv_style_set_text_font(&pagefont, LV_STATE_DEFAULT, defaultFont);

    // list = lv_obj_get_style_list(lv_disp_get_layer_top(NULL), LV_OBJ_PART_MAIN);
    // _lv_style_list_add_style(list, &pagefont);

    /* Create all screens using the theme */
    for(uint8_t i = 0; i < (sizeof pages / sizeof *pages); i++) {
        pages[i] = lv_obj_create(NULL, NULL);
        //  list     = lv_obj_get_style_list(pages[i], LV_OBJ_PART_MAIN);
        // _lv_style_list_add_style(list, &pagefont);
    }

#if HASP_USE_WIFI > 0
    if(!wifiShowAP()) {
        haspDisconnect();
    }
#endif

    haspLoadPage(haspPagesPath);
    haspSetPage(haspStartPage);

    // lv_obj_t * obj = lv_btn_create(pages[0], NULL);
    // lv_obj_set_size(obj, 100, 100);
    // lv_obj_set_user_data(obj, (lv_obj_user_data_t)15);
    // /* lv_obj_t * label ; */
    // lv_label_create(obj, NULL);
    // // haspSetOpacity(obj, LV_OPA_COVER);
    // lv_obj_set_event_cb(obj, btn_event_handler);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

void haspLoop(void)
{}

/*
void hasp_background(uint16_t pageid, uint16_t imageid)
{
    lv_obj_t * page = get_page(pageid);
    if(!page) return;

    return;

    page               = lv_scr_act();
    lv_obj_t * thisobj = lv_obj_get_child_back(page, NULL);

    if(!thisobj) return;

    switch (imageid)
    {
    case 0:
        lv_img_set_src(thisobj, &frame00);
        break;
    case 1:
        lv_img_set_src(thisobj, &frame02);
        break;
    case 2:
        lv_img_set_src(thisobj, &frame04);
        break;
    case 3:
        lv_img_set_src(thisobj, &frame06);
        break;
    case 4:
        lv_img_set_src(thisobj, &frame08);
        break;
    case 5:
        lv_img_set_src(thisobj, &frame10);
        break;
    case 6:
        lv_img_set_src(thisobj, &frame12);
        break;
    case 7:
        lv_img_set_src(thisobj, &frame14);
        break;
    }
    //printf("Image set to %u\n", imageid);

    lv_img_set_auto_size(thisobj, false);
    lv_obj_set_width(thisobj, lv_disp_get_hor_res(NULL));
    lv_obj_set_height(thisobj, lv_disp_get_ver_res(NULL));
    // lv_obj_set_protect(wp, LV_PROTECT_POS);
    // lv_obj_invalidate(thisobj);
}*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Called when a a list button is clicked on the List tab
 * @param btn pointer to a list button
 * @param event type of event that occured
 */
void IRAM_ATTR btn_event_handler(lv_obj_t * obj, lv_event_t event)
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), PSTR("HASP: "));

    switch(event) {
        case LV_EVENT_PRESSED:
            memcpy_P(buffer, PSTR("DOWN"), sizeof(buffer));
            break;
        case LV_EVENT_CLICKED:
            // UP = the same object was release then was pressed and press was not lost!
            memcpy_P(buffer, PSTR("UP"), sizeof(buffer));
            break;
        case LV_EVENT_SHORT_CLICKED:
            memcpy_P(buffer, PSTR("SHORT"), sizeof(buffer));
            break;
        case LV_EVENT_LONG_PRESSED:
            memcpy_P(buffer, PSTR("LONG"), sizeof(buffer));
            break;
        case LV_EVENT_LONG_PRESSED_REPEAT:
            memcpy_P(buffer, PSTR("HOLD"), sizeof(buffer));
            break;
        case LV_EVENT_PRESS_LOST:
            memcpy_P(buffer, PSTR("LOST"), sizeof(buffer));
            break;
        case LV_EVENT_PRESSING:
        case LV_EVENT_FOCUSED:
        case LV_EVENT_DEFOCUSED:
        case LV_EVENT_RELEASED:
            return;

        case LV_EVENT_VALUE_CHANGED:
            strcat_P(buffer, PSTR("Value Changed"));
            Log.notice(buffer);
            return;

        case LV_EVENT_DELETE:
            strcat_P(buffer, PSTR("Object deleted"));
            Log.notice(buffer, event);
            return;
        default:
            strcat_P(buffer, PSTR("HASP : Unknown Event % d occured"));
            Log.warning(buffer, event);
            return;
    }

    if(obj == lv_disp_get_layer_sys(NULL)) {
#if HASP_USE_MQTT > 0
        mqtt_send_state(F("wakeuptouch"), buffer);
#endif
    } else {
        hasp_send_obj_attribute_event(obj, buffer);
    }
}

// ##################### Event Handlers by Version ########################################################

/*static void btnmap_event_handler(lv_obj_t * obj, lv_event_t event)
{
     if(event == LV_EVENT_VALUE_CHANGED) haspSendNewValue(obj, lv_btnmatrix_get_pressed_btn(obj));
}*/

void IRAM_ATTR toggle_event_handler(lv_obj_t * obj, lv_event_t event)
{
    bool toggled =
        lv_btn_get_state(obj) == LV_BTN_STATE_CHECKED_PRESSED || lv_btn_get_state(obj) == LV_BTN_STATE_CHECKED_RELEASED;
    if(event == LV_EVENT_VALUE_CHANGED) hasp_send_obj_attribute_val(obj, toggled);
}

static void switch_event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) hasp_send_obj_attribute_val(obj, lv_switch_get_state(obj));
}

static void checkbox_event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) hasp_send_obj_attribute_val(obj, lv_checkbox_is_checked(obj));
}

static void ddlist_event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        hasp_send_obj_attribute_val(obj, lv_dropdown_get_selected(obj));
        char buffer[128];
        lv_dropdown_get_selected_str(obj, buffer, sizeof(buffer));
        hasp_send_obj_attribute_txt(obj, buffer);
    }
}

static void slider_event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) hasp_send_obj_attribute_val(obj, lv_slider_get_value(obj));
}

static void cpicker_event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) hasp_send_obj_attribute_color(obj, "color", lv_cpicker_get_color(obj));
}

static void roller_event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        hasp_send_obj_attribute_val(obj, lv_roller_get_selected(obj));
        char buffer[128];
        lv_roller_get_selected_str(obj, buffer, sizeof(buffer));
        hasp_send_obj_attribute_txt(obj, buffer);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

String haspGetVersion()
{
    char buffer[128];
    snprintf_P(buffer, sizeof(buffer), "%u.%u.%u", HASP_VERSION_MAJOR, HASP_VERSION_MINOR, HASP_VERSION_REVISION);
    return buffer;
}

void haspClearPage(uint16_t pageid)
{
    lv_obj_t * page = get_page(pageid);
    if(!page) {
        Log.warning(F("HASP: Page ID %u not defined"), pageid);
    } else if(page == lv_layer_sys() /*|| page == lv_layer_top()*/) {
        Log.warning(F("HASP: Cannot clear system layer"));
    } else {
        Log.notice(F("HASP: Clearing page %u"), pageid);
        lv_obj_clean(pages[pageid]);
    }
}

uint8_t haspGetPage()
{
    return current_page;
}

void haspSetPage(uint8_t pageid)
{
    lv_obj_t * page = get_page(pageid);
    if(!page) {
        Log.warning(F("HASP: Page ID %u not defined"), pageid);
    } else if(page == lv_layer_sys() || page == lv_layer_top()) {
        Log.warning(F("HASP: %sCannot change to a layer"));
    } else {
        // if(pageid != current_page) {
        Log.notice(F("HASP: Changing page to %u"), pageid);
        current_page = pageid;
        lv_scr_load(page);
        //}
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void haspNewObject(const JsonObject & config, uint8_t & saved_page_id)
{
    /* Validate page */
    uint8_t pageid = config[F("page")].isNull() ? current_page : config[F("page")].as<uint8_t>();

    /* Page selection */
    lv_obj_t * page = get_page(pageid);
    if(!page) {
        Log.warning(F("HASP: Page ID %u not defined"), pageid);
        return;
    }
    /* save the current pageid */
    saved_page_id = pageid;

    /* Validate type */
    if(config[F("objid")].isNull()) return; // comments

    lv_obj_t * parent_obj = page;
    if(!config[F("parentid")].isNull()) {
        uint8_t parentid = config[F("parentid")].as<uint8_t>();
        parent_obj       = hasp_find_obj_from_id(page, parentid);
        if(!parent_obj) {
            Log.warning(F("HASP: Parent ID p[%u].b[%u] not found"), pageid, parentid);
            parent_obj = page; // create on the page instead ??
        } else {
            Log.trace(F("HASP: Parent ID p[%u].b[%u] found"), pageid, parentid);
        }
    }

    uint8_t objid = config[F("objid")].as<uint8_t>();
    uint8_t id    = config[F("id")].as<uint8_t>();

    /* Define Objects*/
    lv_obj_t * obj = hasp_find_obj_from_id(parent_obj, id);
    if(obj) {
        Log.warning(F("HASP: Object ID %u already exists!"), id);
        return;
    }

    switch(objid) {
        /* ----- Basic Objects ------ */
        case LV_HASP_BUTTON: {
            obj = lv_btn_create(parent_obj, NULL);
            lv_label_create(obj, NULL);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
        case LV_HASP_CHECKBOX: {
            obj = lv_checkbox_create(parent_obj, NULL);
            lv_obj_set_event_cb(obj, checkbox_event_handler);
            break;
        }
        case LV_HASP_LABEL: {
            obj = lv_label_create(parent_obj, NULL);
            /* click area padding */
            uint8_t padh = config[F("padh")].as<uint8_t>();
            uint8_t padv = config[F("padv")].as<uint8_t>();
            /* text align */
            if(padh > 0 || padv > 0) {
                lv_obj_set_ext_click_area(obj, padh, padh, padv, padv);
            }
            if(!config[F("align")].isNull()) {
                lv_label_set_align(obj, LV_LABEL_ALIGN_CENTER);
            }
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
        case LV_HASP_IMAGE: {
            obj = lv_img_create(parent_obj, NULL);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
        case LV_HASP_ARC: {
            obj = lv_arc_create(parent_obj, NULL);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
        case LV_HASP_CONTAINER: {
            obj = lv_cont_create(parent_obj, NULL);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
        case LV_HASP_OBJECT: {
            obj = lv_obj_create(parent_obj, NULL);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
        case LV_HASP_PAGE: {
            obj = lv_page_create(parent_obj, NULL);
            break;
        }
        case LV_HASP_TABVIEW: {
            obj = lv_tabview_create(parent_obj, NULL);
            lv_obj_t * tab;
            tab = lv_tabview_add_tab(obj, "tab 1");
            lv_obj_set_user_data(tab, id + 1);
            tab = lv_tabview_add_tab(obj, "tab 2");
            lv_obj_set_user_data(tab, id + 2);
            tab = lv_tabview_add_tab(obj, "tab 3");
            lv_obj_set_user_data(tab, id + 3);
            break;
        }
        case LV_HASP_TILEVIEW: {
            obj = lv_tileview_create(parent_obj, NULL);
            break;
        }

        /* ----- Color Objects ------ */
        case LV_HASP_CPICKER: {
            obj = lv_cpicker_create(parent_obj, NULL);
            lv_obj_set_event_cb(obj, cpicker_event_handler);
            break;
        }
#if LV_USE_PRELOAD != 0
        case LV_HASP_PRELOADER: {
            obj = lv_spinner_create(parent_obj, NULL);
            break;
        }
#endif
        /* ----- Range Objects ------ */
        case LV_HASP_SLIDER: {
            obj = lv_slider_create(parent_obj, NULL);
            lv_slider_set_range(obj, 0, 100);
            lv_obj_set_event_cb(obj, slider_event_handler);
            // bool knobin = config[F("knobin")].as<bool>() | true;
            // lv_slider_set_knob_in(obj, knobin);
            break;
        }
        case LV_HASP_GAUGE: {
            obj = lv_gauge_create(parent_obj, NULL);
            lv_gauge_set_range(obj, 0, 100);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
        case LV_HASP_BAR: {
            obj = lv_bar_create(parent_obj, NULL);
            lv_bar_set_range(obj, 0, 100);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
        case LV_HASP_LMETER: {
            obj = lv_linemeter_create(parent_obj, NULL);
            lv_linemeter_set_range(obj, 0, 100);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }

        /* ----- On/Off Objects ------ */
        case LV_HASP_SWITCH: {
            obj = lv_switch_create(parent_obj, NULL);
            lv_obj_set_event_cb(obj, switch_event_handler);
            break;
        }
        case LV_HASP_LED: {
            obj = lv_led_create(parent_obj, NULL);
            lv_obj_set_event_cb(obj, btn_event_handler);
            break;
        }
            /**/
        case LV_HASP_DDLIST: {
            obj = lv_dropdown_create(parent_obj, NULL);
            // lv_dropdown_set_fix_width(obj, width);
            lv_dropdown_set_draw_arrow(obj, true);
            // lv_dropdown_set_anim_time(obj, 200);
            lv_obj_set_top(obj, true);
            // lv_obj_align(obj, NULL, LV_ALIGN_IN_TOP_MID, 0, 20);
            lv_obj_set_event_cb(obj, ddlist_event_handler);
            break;
        }
        case LV_HASP_ROLLER: {
            obj           = lv_roller_create(parent_obj, NULL);
            // lv_roller_set_fix_width(obj, width);
            // lv_obj_align(obj, NULL, LV_ALIGN_IN_TOP_MID, 0, 20);
            lv_obj_set_event_cb(obj, roller_event_handler);
            break;
        }

        /* ----- Other Object ------ */
        default:
            Log.warning(F("HASP: Unsupported Object ID %u"), objid);
            return;
    }

    if(!obj) {
        Log.warning(F("HASP: Object is NULL, skipping..."));
        return;
    }

    lv_obj_set_user_data(obj, id);

    /* do not process these attributes */
    config.remove(F("page"));
    config.remove(F("id"));
    config.remove(F("objid"));
    config.remove(F("parentid"));
    String v;

    for(JsonPair keyValue : config) {
        v = keyValue.value().as<String>();
        hasp_process_obj_attribute(obj, keyValue.key().c_str(), v.c_str(), true);
        // Log.trace(F("     * %s => %s"), keyValue.key().c_str(), v.c_str());
    }

    /** testing start **/
    lv_obj_user_data_t temp;
    if(!FindIdFromObj(obj, &pageid, &temp)) {
        Log.error(F("HASP: Lost track of the created object, not found!"));
        return;
    }
    /** testing end **/

    lv_obj_type_t list;
    lv_obj_get_type(obj, &list);
    Log.verbose(F("HASP:     * p[%u].b[%u] = %s"), pageid, temp, list.type[0]);

    /* Double-check */
    lv_obj_t * test = hasp_find_obj_from_id(pageid, (uint8_t)temp);
    if(test != obj) {
        Log.error(F("HASP: Objects DO NOT match!"));
    } else {
        // Log.trace(F("Objects match!"));
    }
}

void haspLoadPage(const char * pages)
{
#if HASP_USE_SPIFFS > 0
    if(pages[0] == '\0') return;

    if(!SPIFFS.begin()) {
        Log.error(F("HASP: FS not mounted. Failed to load %s"), pages);
        return;
    }

    if(!SPIFFS.exists(pages)) {
        Log.error(F("HASP: Non existing file %s"), pages);
        return;
    }

    Log.notice(F("HASP: Loading file %s"), pages);

    File file = SPIFFS.open(pages, "r");
    dispatchJsonl(file);
    file.close();

    Log.notice(F("HASP: File %s loaded"), pages);
#else

#if HASP_USE_EEPROM > 0
    Log.notice(F("HASP: Loading jsonl from EEPROM..."));
    EepromStream eepromStream(4096, 1024);
    dispatchJsonl(eepromStream);
    Log.notice(F("HASP: Loaded jsonl from EEPROM"));
#endif

#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool haspGetConfig(const JsonObject & settings)
{
    bool changed = false;

    if(haspStartPage != settings[FPSTR(F_CONFIG_STARTPAGE)].as<uint8_t>()) changed = true;
    settings[FPSTR(F_CONFIG_STARTPAGE)] = haspStartPage;

    if(haspStartDim != settings[FPSTR(F_CONFIG_STARTDIM)].as<uint8_t>()) changed = true;
    settings[FPSTR(F_CONFIG_STARTDIM)] = haspStartDim;

    if(haspThemeId != settings[FPSTR(F_CONFIG_THEME)].as<uint8_t>()) changed = true;
    settings[FPSTR(F_CONFIG_THEME)] = haspThemeId;

    if(haspThemeHue != settings[FPSTR(F_CONFIG_HUE)].as<uint16_t>()) changed = true;
    settings[FPSTR(F_CONFIG_HUE)] = haspThemeHue;

    if(strcmp(haspZiFontPath, settings[FPSTR(F_CONFIG_ZIFONT)].as<String>().c_str()) != 0) changed = true;
    settings[FPSTR(F_CONFIG_ZIFONT)] = haspZiFontPath;

    if(strcmp(haspPagesPath, settings[FPSTR(F_CONFIG_PAGES)].as<String>().c_str()) != 0) changed = true;
    settings[FPSTR(F_CONFIG_PAGES)] = haspPagesPath;

    if(changed) configOutput(settings);
    return changed;
}

/** Set HASP Configuration.
 *
 * Read the settings from json and sets the application variables.
 *
 * @note: data pixel should be formated to uint32_t RGBA. Imagemagick requirements.
 *
 * @param[in] settings    JsonObject with the config settings.
 **/
bool haspSetConfig(const JsonObject & settings)
{
    configOutput(settings);
    bool changed = false;

    changed |= configSet(haspStartPage, settings[FPSTR(F_CONFIG_STARTPAGE)], PSTR("haspStartPage"));
    changed |= configSet(haspStartDim, settings[FPSTR(F_CONFIG_STARTDIM)], PSTR("haspStartDim"));
    changed |= configSet(haspThemeId, settings[FPSTR(F_CONFIG_THEME)], PSTR("haspThemeId"));
    changed |= configSet(haspThemeHue, settings[FPSTR(F_CONFIG_HUE)], PSTR("haspThemeHue"));

    if(!settings[FPSTR(F_CONFIG_PAGES)].isNull()) {
        changed |= strcmp(haspPagesPath, settings[FPSTR(F_CONFIG_PAGES)]) != 0;
        strncpy(haspPagesPath, settings[FPSTR(F_CONFIG_PAGES)], sizeof(haspPagesPath));
    }

    if(!settings[FPSTR(F_CONFIG_ZIFONT)].isNull()) {
        changed |= strcmp(haspZiFontPath, settings[FPSTR(F_CONFIG_ZIFONT)]) != 0;
        strncpy(haspZiFontPath, settings[FPSTR(F_CONFIG_ZIFONT)], sizeof(haspZiFontPath));
    }

    return changed;
}

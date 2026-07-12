/*
 * Enter Remote (BLE) for Flipper Zero
 * ------------------------------------
 * A Bluetooth LE HID keyboard remote, modelled on the stock "Keynote"
 * profile of the Bluetooth Remote app, with ONE difference:
 *
 *      OK button  ->  ENTER   (the stock Keynote sends Space here)
 *
 * Everything else behaves like a presentation clicker:
 *      Up / Left   ->  previous slide (Arrow Up / Arrow Left)
 *      Down / Right->  next slide     (Arrow Down / Arrow Right)
 *      Short Back  ->  Escape (quit slideshow mode)
 *      Hold Back   ->  exit this app
 *
 * Pair your Flipper from the host computer's Bluetooth settings, open a
 * presentation, and click away.
 */

#include <furi.h>
#include <furi_hal_bt.h>

#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>

#define TAG "EnterRemote"

/*
 * Standard USB HID keyboard "usage IDs". These are part of the USB HID
 * spec and never change, so defining them here keeps the app independent
 * of any particular firmware header layout.
 */
#define KEY_ENTER 0x28 /* Return / Enter                 */
#define KEY_ESC 0x29 /* Escape (used to quit slideshow)  */
#define KEY_RIGHT 0x4F /* Right Arrow                    */
#define KEY_LEFT 0x50 /* Left Arrow                     */
#define KEY_DOWN 0x51 /* Down Arrow                     */
#define KEY_UP 0x52 /* Up Arrow                       */

/*
 * Keep the HID pairing keys in the app's own storage folder so we don't
 * disturb the Flipper's default (serial) Bluetooth pairing.
 */
#define HID_KEYS_DIR EXT_PATH("apps_data/enter_remote")
#define HID_KEYS_PATH EXT_PATH("apps_data/enter_remote/.bt_hid.keys")

/*
 * Advertise as our OWN Bluetooth device, separate from the Flipper's serial
 * Bluetooth and the stock Bluetooth Remote. A non-zero mac_xor gives this app
 * a unique BLE MAC address, so the host sees a brand-new device to pair with
 * instead of colliding with an existing "Flipper <name>" bond -- a collision
 * shows up as the host rapidly connecting and disconnecting because its saved
 * pairing keys don't match this app's. The prefix (< 8 chars) makes the app
 * easy to spot in the host's Bluetooth list.
 */
static const BleProfileHidParams hid_params = {
    .device_name_prefix = "Enter",
    .mac_xor = 0x00E1,
};

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* input_queue;
    Gui* gui;
    ViewPort* view_port;
    Bt* bt;
    NotificationApp* notifications;
    FuriHalBleProfileBase* hid_profile;
    bool connected;
} EnterRemote;

/* ------------------------------------------------------------------ */
/* GUI                                                                */
/* ------------------------------------------------------------------ */

static void enter_remote_draw_callback(Canvas* canvas, void* ctx) {
    EnterRemote* app = ctx;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool connected = app->connected;
    furi_mutex_release(app->mutex);

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Enter Remote");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 23, connected ? "Connected" : "Pair \"Enter...\" device");

    canvas_draw_line(canvas, 0, 26, 128, 26);

    canvas_draw_str(canvas, 2, 37, "OK = Enter    Back = Esc");
    canvas_draw_str(canvas, 2, 47, "Up/Left = Previous");
    canvas_draw_str(canvas, 2, 57, "Down/Right = Next");
    canvas_draw_str(canvas, 2, 64, "Hold Back to exit");
}

static void enter_remote_input_callback(InputEvent* event, void* ctx) {
    EnterRemote* app = ctx;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

/* ------------------------------------------------------------------ */
/* Input handling                                                     */
/* ------------------------------------------------------------------ */

/* Returns false when the app should exit. */
static bool enter_remote_handle_input(EnterRemote* app, InputEvent* event) {
    if(event->key == InputKeyBack) {
        if(event->type == InputTypeShort) {
            /* Tap Escape: quits slideshow mode in most presentation apps */
            ble_profile_hid_kb_press(app->hid_profile, KEY_ESC);
            furi_delay_ms(12);
            ble_profile_hid_kb_release(app->hid_profile, KEY_ESC);
        } else if(event->type == InputTypeLong) {
            return false; /* hold Back -> quit the app */
        }
        return true;
    }

    uint16_t key = 0;
    switch(event->key) {
    case InputKeyOk:
        key = KEY_ENTER; /* <-- the change: Enter instead of Space */
        break;
    case InputKeyUp:
        key = KEY_UP;
        break;
    case InputKeyDown:
        key = KEY_DOWN;
        break;
    case InputKeyLeft:
        key = KEY_LEFT;
        break;
    case InputKeyRight:
        key = KEY_RIGHT;
        break;
    default:
        return true;
    }

    /* Mirror the real key: hold it while the Flipper button is held. */
    if(event->type == InputTypePress) {
        ble_profile_hid_kb_press(app->hid_profile, key);
    } else if(event->type == InputTypeRelease) {
        ble_profile_hid_kb_release(app->hid_profile, key);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Bluetooth                                                          */
/* ------------------------------------------------------------------ */

static void enter_remote_bt_status_callback(BtStatus status, void* context) {
    EnterRemote* app = context;
    bool connected = (status == BtStatusConnected);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->connected = connected;
    furi_mutex_release(app->mutex);

    /* Blue LED on when a host is connected, like the stock remote. */
    notification_message(
        app->notifications, connected ? &sequence_set_blue_255 : &sequence_reset_blue);
    view_port_update(app->view_port);
}

static void enter_remote_bt_start(EnterRemote* app) {
    /* Ensure the HID keys folder exists (harmless if it already does). */
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, HID_KEYS_DIR);
    furi_record_close(RECORD_STORAGE);

    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(app->bt, HID_KEYS_PATH);

    app->hid_profile = bt_profile_start(app->bt, ble_profile_hid, (void*)&hid_params);
    furi_check(app->hid_profile);

    furi_hal_bt_start_advertising();
    bt_set_status_changed_callback(app->bt, enter_remote_bt_status_callback, app);
}

static void enter_remote_bt_stop(EnterRemote* app) {
    bt_set_status_changed_callback(app->bt, NULL, NULL);
    notification_message(app->notifications, &sequence_reset_blue);

    bt_disconnect(app->bt);
    furi_delay_ms(200);

    /* Restore the Flipper's normal Bluetooth so serial/other apps work again. */
    bt_keys_storage_set_default_path(app->bt);
    furi_check(bt_profile_restore_default(app->bt));
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

static EnterRemote* enter_remote_alloc(void) {
    EnterRemote* app = malloc(sizeof(EnterRemote));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->connected = false;
    app->hid_profile = NULL;

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, enter_remote_draw_callback, app);
    view_port_input_callback_set(app->view_port, enter_remote_input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->bt = furi_record_open(RECORD_BT);

    return app;
}

static void enter_remote_free(EnterRemote* app) {
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_BT);

    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);
}

int32_t enter_remote_app(void* p) {
    UNUSED(p);
    EnterRemote* app = enter_remote_alloc();
    enter_remote_bt_start(app);

    InputEvent event;
    for(bool running = true; running;) {
        if(furi_message_queue_get(app->input_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(!enter_remote_handle_input(app, &event)) {
                running = false;
            }
        }
    }

    ble_profile_hid_kb_release_all(app->hid_profile);
    enter_remote_bt_stop(app);
    enter_remote_free(app);
    return 0;
}

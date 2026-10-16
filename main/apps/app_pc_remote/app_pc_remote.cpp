#include "app_pc_remote.h"
#include "pc_remote_ble.h"
#include "cJSON.h"
#include <cstring>
#include <algorithm>
#include <hal.h>
#include <apps/utils/theme.h>
#include <apps/utils/common.h>
#include <assets.h>
#include <esp_system.h>
#include <hal/keyboard/keyboard.h>
#include <apps/app_keyboard/assets/keyboard_big.h>
#include <apps/app_keyboard/assets/keyboard_small.h>

AppPcRemote::AppPcRemote()
{
    setAppInfo().name = "PC Remote";
    setAppInfo().userData = new AppIcon_t(image_data_keyboard_big, image_data_keyboard_small);
}
AppPcRemote::~AppPcRemote()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}
void AppPcRemote::onOpen()
{
    pc_remote_ble_start();
    last_draw_ = 0;
    key_slot_ = GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& ev) {
        if (!ev.state || ev.isModifier) return;
        if (ev.keyCode == KEY_UP && count_) selected_ = (selected_ + count_ - 1) % count_;
        if (ev.keyCode == KEY_DOWN && count_) selected_ = (selected_ + 1) % count_;
        if (ev.keyCode == KEY_ENTER && ready_ && count_) pc_remote_ble_send_task(selected_);
        last_draw_ = 0;
    });
}
void AppPcRemote::receive_menu()
{
    char payload[1025];
    while (pc_remote_ble_take_menu(payload, sizeof(payload)) > 0) {
        cJSON *root = cJSON_Parse(payload);
        cJSON *page = cJSON_GetObjectItemCaseSensitive(root, "page");
        cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
        cJSON *menu = cJSON_GetObjectItemCaseSensitive(root, "menu");
        if (!cJSON_IsNumber(page) || !cJSON_IsNumber(pages) || !cJSON_IsArray(menu)) {
            ready_ = false; cJSON_Delete(root); continue;
        }
        int n = page->valueint, total = pages->valueint;
        if (n == 0) { count_ = 0; selected_ = 0; next_page_ = 0; pages_ = total; ready_ = false; }
        if (n != next_page_ || total != pages_ || total < 1 || total > 16) {
            ready_ = false; cJSON_Delete(root); continue;
        }
        bool valid = true;
        cJSON *entry;
        cJSON_ArrayForEach(entry, menu) {
            cJSON *label = cJSON_GetObjectItemCaseSensitive(entry, "label");
            if (!cJSON_IsString(label) || count_ >= 64 || strlen(label->valuestring) >= sizeof(labels_[0])) {
                valid = false; break;
            }
            strcpy(labels_[count_++], label->valuestring);
        }
        if (valid) { ++next_page_; ready_ = next_page_ == pages_; }
        else { ready_ = false; next_page_ = -1; }
        cJSON_Delete(root);
        last_draw_ = 0;
    }
}
void AppPcRemote::onRunning()
{
    bool connected = pc_remote_ble_connected();
    if (connected != was_connected_) {
        ready_ = false; count_ = 0; next_page_ = 0;
        was_connected_ = connected; last_draw_ = 0;
    }
    receive_menu();
    if (GetHAL().millis() - last_draw_ >= 500 || last_draw_ == 0) {
        last_draw_ = GetHAL().millis();
        auto &screen = GetHAL().canvas;
        screen.fillScreen(THEME_COLOR_BG);
        screen.setTextSize(1);
        screen.setTextWrap(false);  // Long labels must not create hidden rows.
        const int line = screen.fontHeight();
        const int rows = std::max(1, std::min(3, (static_cast<int>(screen.height()) - 3 * line) / line));
        screen.setCursor(0, 0);
        screen.setTextColor(TFT_CYAN, THEME_COLOR_BG);
        screen.print(pc_remote_ble_connected() ? "PC Remote: Connected" : "PC Remote: Waiting");
        int state = pc_remote_ble_command_state();
        screen.setCursor(0, line);
        screen.setTextColor(state == 2 ? TFT_GREEN : TFT_WHITE, THEME_COLOR_BG);
        screen.print(state == 1 ? "Waiting for PC..." :
                     state == 2 ? "Result: OK" :
                     state == 3 ? "Result: ERROR" : "Ready");
        screen.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        screen.setCursor(0, 2 * line);
        if (!ready_) screen.print("Waiting for menu...");
        else if (!count_) screen.print("No tasks configured");
        else {
            int first = (selected_ / rows) * rows;
            for (int i = first; i < count_ && i < first + rows; ++i) {
                screen.setCursor(0, (2 + i - first) * line);
                screen.printf("%c %s", i == selected_ ? '>' : ' ', labels_[i]);
            }
        }
        screen.setCursor(0, screen.height() - line);
        screen.print("Up/Down Enter G0");
        GetHAL().pushCanvas();
    }
    if (GetHAL().homeButton.wasClicked()) close();
}
void AppPcRemote::onClose()
{
    // Matches existing BLE keyboard lifecycle: reboot ensures BLE host is
    // fully reset before the launcher or HID keyboard can be used again.
    if (key_slot_ >= 0) GetHAL().keyboard.onKeyEvent.disconnect(key_slot_);
    esp_restart();
}



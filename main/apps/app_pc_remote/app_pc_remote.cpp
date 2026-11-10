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
        // Ignore Ctrl/Alt/Meta chords, including Enter: no terminal controls.
        if (GetHAL().keyboard.getModifierMask() &
            (KEY_MOD_LCTRL | KEY_MOD_LALT | KEY_MOD_LMETA)) return;
        if (editing_) {
            if (ev.keyCode == KEY_ESC) {
                if (preview_) preview_ = false;
                else editing_ = false;
            } else if (preview_) {
                if (ev.keyCode == KEY_ENTER) {
                    pc_remote_ble_send_text(selected_, draft_, draft_length_);
                    editing_ = preview_ = false;
                } else if (ev.keyCode == KEY_DOWN) ++preview_page_;
                else if (ev.keyCode == KEY_UP && preview_page_) --preview_page_;
            } else if (ev.keyCode == KEY_DELETE) {
                draft_length_ = 0; draft_[0] = 0;
            } else if (ev.keyCode == KEY_BACKSPACE) {
                if (draft_length_) draft_[--draft_length_] = 0;
            } else if (ev.keyCode == KEY_ENTER) {
                if (draft_length_) { preview_ = true; preview_page_ = 0; }
            } else if (ev.keyName && ev.keyName[0] && !ev.keyName[1] &&
                       (unsigned char)ev.keyName[0] >= 0x20 &&
                       (unsigned char)ev.keyName[0] <= 0x7e && draft_length_ < 512) {
                draft_[draft_length_++] = ev.keyName[0]; draft_[draft_length_] = 0;
            }
            last_draw_ = 0; return;
        }
        if (pc_remote_ble_command_state() == 1) return;
        if (ev.keyCode == KEY_UP && count_) selected_ = (selected_ + count_ - 1) % count_;
        if (ev.keyCode == KEY_DOWN && count_) selected_ = (selected_ + 1) % count_;
        if (ev.keyCode == KEY_ENTER && ready_ && count_) {
            if (text_entry_[selected_]) {
                if (draft_index_ != selected_) {
                    draft_length_ = 0; draft_[0] = 0; draft_index_ = selected_;
                }
                editing_ = true; preview_ = false;
            } else pc_remote_ble_send_task(selected_);
        }
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
        if (n == 0) { draft_index_ = -1; editing_ = preview_ = false; count_ = 0; selected_ = 0; next_page_ = 0; pages_ = total; ready_ = false; }
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
            cJSON *input = cJSON_GetObjectItemCaseSensitive(entry, "input");
            if (input && (!cJSON_IsString(input) || strcmp(input->valuestring, "text") != 0)) {
                valid = false; break;
            }
            text_entry_[count_] = input != nullptr;
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
    pc_remote_ble_poll();
    bool connected = pc_remote_ble_connected();
    if (connected != was_connected_) {
        ready_ = false; count_ = 0; next_page_ = 0; editing_ = preview_ = false;
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
        screen.print(editing_ ? (preview_ ? "INERT SINK: review" : "Not sent; Fn+Del clears") :
                     state == 1 ? "Waiting for PC..." :
                     state == 2 ? "PC receipt: OK" :
                     state == 3 ? "Not accepted / ERROR" :
                     state == 4 ? "Receipt UNKNOWN" :
                     state == 5 ? "Restart required" : "Ready");
        screen.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        screen.setCursor(0, 2 * line);
        if (editing_) {
            const size_t columns = std::max(1, static_cast<int>(screen.width()) / std::max(1, static_cast<int>(screen.textWidth("W"))));
            const size_t body_rows = std::max(1, static_cast<int>(screen.height()) / line - 4);
            const size_t page_size = columns * body_rows;
            const size_t last_page = draft_length_ ? (draft_length_ - 1) / page_size : 0;
            if (preview_page_ > last_page) preview_page_ = last_page;
            const size_t start = preview_ ? preview_page_ * page_size :
                (draft_length_ > page_size ? draft_length_ - page_size : 0);
            screen.printf("%s %u/512%s", preview_ ? "REVIEW" : "ASCII",
                static_cast<unsigned>(draft_length_), draft_length_ == 512 ? " FULL" : "");
            if (preview_) screen.printf(" p%u/%u", static_cast<unsigned>(preview_page_ + 1),
                static_cast<unsigned>(last_page + 1));
            for (size_t row = 0; row < body_rows; ++row) {
                size_t offset = start + row * columns;
                if (offset >= draft_length_) break;
                screen.setCursor(0, (3 + row) * line);
                size_t length = std::min(columns, draft_length_ - offset);
                for (size_t j = 0; j < length; ++j) screen.print(draft_[offset+j]);
            }
        }
        else if (!ready_) screen.print("Waiting for menu...");
        else if (!count_) screen.print("No tasks configured");
        else {
            int first = (selected_ / rows) * rows;
            for (int i = first; i < count_ && i < first + rows; ++i) {
                screen.setCursor(0, (2 + i - first) * line);
                screen.printf("%c %s", i == selected_ ? '>' : ' ', labels_[i]);
            }
        }
        screen.setCursor(0, screen.height() - line);
        screen.print(editing_ ? (preview_ ? "Enter SEND Esc edit ^v" : "Enter review Esc cancel") :
                     "Up/Down Enter G0");
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



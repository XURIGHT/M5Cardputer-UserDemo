#include "app_pc_remote.h"
#include "pc_remote_ble.h"
#include <hal.h>
#include <apps/utils/theme.h>
#include <apps/utils/common.h>
#include <assets.h>
#include <esp_system.h>
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
}
void AppPcRemote::onRunning()
{
    if (GetHAL().millis() - last_draw_ >= 500 || last_draw_ == 0) {
        last_draw_ = GetHAL().millis();
        auto &screen = GetHAL().canvas;
        screen.fillScreen(THEME_COLOR_BG);
        screen.setCursor(0, 0);
        screen.setTextSize(1);
        screen.setTextColor(TFT_CYAN, THEME_COLOR_BG);
        screen.println("PC Remote BLE");
        screen.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        screen.println(pc_remote_ble_connected() ? "Connected" : "Advertising...");
        screen.printf("PING count: %d\n", pc_remote_ble_ping_count());
        screen.printf("BLE error: %d\n", pc_remote_ble_last_error());
        screen.println("G0: back (reboot)");
        GetHAL().pushCanvas();
    }
    if (GetHAL().homeButton.wasClicked()) close();
}
void AppPcRemote::onClose()
{
    // Matches existing BLE keyboard lifecycle: reboot ensures BLE host is
    // fully reset before the launcher or HID keyboard can be used again.
    esp_restart();
}

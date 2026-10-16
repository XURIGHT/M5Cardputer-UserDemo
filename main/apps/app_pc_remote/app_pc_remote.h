#pragma once
#include <mooncake.h>
class AppPcRemote : public mooncake::AppAbility {
public:
    AppPcRemote();
    ~AppPcRemote() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    unsigned long last_draw_ = 0;
    char labels_[64][96] = {};
    int count_ = 0, selected_ = 0, next_page_ = 0, pages_ = 0;
    bool ready_ = false, was_connected_ = false;
    void receive_menu();
    int key_slot_ = -1;
};


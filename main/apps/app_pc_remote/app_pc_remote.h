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
};

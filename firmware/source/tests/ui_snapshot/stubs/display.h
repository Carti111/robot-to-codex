#pragma once
#include <lvgl.h>
#include <string>
class Display {
public:
 virtual ~Display() = default;
 virtual void SetTheme(const std::string&) {}
 virtual void UpdateStatusBar(bool = false) {}
 int width() const { return 240; }
 int height() const { return 240; }
 void SetChatVisible(bool) {}
};
class DisplayLockGuard { public: explicit DisplayLockGuard(Display*) {} };

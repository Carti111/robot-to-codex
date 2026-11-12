#include "../main/boards/esp-sparkbot/spark_menu_state.h"
#define CHECK(c) do { if(!(c)) return __LINE__; } while(0)
extern "C" int mainCRTStartup() {
    SparkMenuState menu;
    CHECK(menu.Navigate(1,false,0)); CHECK(menu.page==SparkPage::Clock);
    CHECK(menu.Navigate(1,false,1)); CHECK(menu.page==SparkPage::Codex);
    CHECK(!menu.Navigate(1,true,100)); CHECK(menu.preview==3 && menu.page==SparkPage::Codex);
    CHECK(!menu.Tick(true,3099)); CHECK(menu.preview==3);
    CHECK(!menu.Tick(true,3100)); CHECK(menu.preview==-1 && menu.page==SparkPage::Codex);
    CHECK(!menu.Navigate(1,true,4000)); CHECK(!menu.Confirm(true,4100)); CHECK(menu.exiting);
    CHECK(!menu.Tick(true,4200)); CHECK(menu.page==SparkPage::Codex);
    CHECK(menu.Tick(false,4300)); CHECK(menu.page==SparkPage::Muyu && !menu.exiting);
    menu.page=SparkPage::Codex;
    CHECK(!menu.Navigate(-1,true,5000)); CHECK(!menu.Confirm(true,8000)); CHECK(!menu.exiting);
    CHECK(menu.page==SparkPage::Codex);
    CHECK(!menu.Navigate(1,true,9000));
    CHECK(!menu.Navigate(1,true,12000)); CHECK(menu.preview==3);
    CHECK(!menu.Confirm(true,12001)); CHECK(menu.exiting);
    CHECK(menu.Tick(false,12002)); CHECK(menu.page==SparkPage::Muyu);
    return 0;
}

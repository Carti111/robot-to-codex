#include "../main/boards/common/camera_preview_copy.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
extern "C" int mainCRTStartup() {
    const uint16_t source[] = {0x3412, 0xcdab, 0xff00, 0xaa55};
    uint16_t output[] = {0xbeef, 0, 0, 0, 0, 0xbeef};
    CopyRgb565Preview(output + 1, source, 2, 2, true);
    CHECK(output[0] == 0xbeef && output[5] == 0xbeef);
    CHECK(output[1] == 0x00ff && output[2] == 0x55aa);
    CHECK(output[3] == 0x1234 && output[4] == 0xabcd);
    CopyRgb565Preview(output + 1, source, 2, 2, false);
    CHECK(output[1] == 0x1234 && output[2] == 0xabcd);
    CHECK(output[3] == 0x00ff && output[4] == 0x55aa);
    CopyRgb565Preview(output + 1, source, 1, 1, true);
    CHECK(output[1] == 0x1234 && output[0] == 0xbeef && output[5] == 0xbeef);
    CopyRgb565Preview(output + 1, source, 2, 2, true, true);
    CHECK(output[1] == 0x55aa && output[2] == 0x00ff);
    CHECK(output[3] == 0xabcd && output[4] == 0x1234);
    CopyRgb565Preview(output + 1, source, 2, 2, false, true);
    CHECK(output[1] == 0xabcd && output[2] == 0x1234);
    CHECK(output[3] == 0x55aa && output[4] == 0x00ff);
    CHECK(output[0] == 0xbeef && output[5] == 0xbeef);
    return 0;
}

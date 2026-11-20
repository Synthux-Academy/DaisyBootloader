// Bench check (a board in update mode, on a Mac or Linux): reads what Windows reads
// to install WinUSB by itself (../../specs/audrey-bootloader.md, "Windows"), so the
// descriptors are checked before a Windows PC is at hand.
//
//   make -C audrey/tests probe      (needs libusb: brew install libusb)
//
// Finds 0483:DF11 made by "Synthux" (the Audrey bootloader, not ST's ROM loader),
// then checks: bcdUSB 2.01 and bcdDevice 3.00, the BOS descriptor with the
// Microsoft OS 2.0 platform capability, and the descriptor set returned by the
// vendor request (WINUSB and the device interface GUID). Prints PASS or FAIL.
#include <libusb.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(int ok, const char* what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok)
        fails++;
}

int main(void)
{
    static const uint8_t kMsOs20Uuid[16] = {0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
                                            0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F};
    static const char* kGuid = "{ED811D07-88C0-4369-8EFC-6A273C003A55}";

    libusb_context* ctx = NULL;
    if(libusb_init(&ctx) != 0)
    {
        printf("FAIL: libusb didn't start\n");
        return 1;
    }
    libusb_device** list = NULL;
    ssize_t         n    = libusb_get_device_list(ctx, &list);
    libusb_device_handle* h = NULL;
    struct libusb_device_descriptor dd;
    for(ssize_t i = 0; i < n && h == NULL; i++)
    {
        if(libusb_get_device_descriptor(list[i], &dd) != 0 || dd.idVendor != 0x0483
           || dd.idProduct != 0xDF11)
            continue;
        libusb_device_handle* cand = NULL;
        if(libusb_open(list[i], &cand) != 0)
            continue;
        unsigned char maker[64] = {0};
        libusb_get_string_descriptor_ascii(cand, dd.iManufacturer, maker, sizeof(maker));
        if(strcmp((const char*)maker, "Synthux") == 0)
            h = cand;
        else
            libusb_close(cand);
    }
    libusb_free_device_list(list, 1);
    if(h == NULL)
    {
        printf("FAIL: no Audrey bootloader in update mode (0483:DF11 made by Synthux)\n");
        libusb_exit(ctx);
        return 1;
    }

    printf("Audrey bootloader found\n");
    check(dd.bcdUSB == 0x0201, "bcdUSB 2.01 (Windows then asks for the BOS descriptor)");
    check(dd.bcdDevice == 0x0300, "bcdDevice 3.00 (its own release number)");

    uint8_t bos[64] = {0};
    int     len     = libusb_control_transfer(h, 0x80, LIBUSB_REQUEST_GET_DESCRIPTOR,
                                       (LIBUSB_DT_BOS << 8), 0, bos, sizeof(bos), 1000);
    check(len == 33 && bos[1] == LIBUSB_DT_BOS && bos[2] == 33 && bos[4] == 1,
          "BOS descriptor, 33 bytes, one capability");
    check(len == 33 && bos[6] == 0x10 && bos[7] == 0x05
              && memcmp(&bos[9], kMsOs20Uuid, sizeof(kMsOs20Uuid)) == 0,
          "platform capability: Microsoft OS 2.0");
    const uint16_t set_len = len == 33 ? (uint16_t)(bos[29] | (bos[30] << 8)) : 0;
    const uint8_t  vendor  = len == 33 ? bos[31] : 0;
    check(set_len == 162, "descriptor set length 162");

    uint8_t set[256] = {0};
    len = libusb_control_transfer(h, 0xC0, vendor, 0, 7, set, set_len, 1000);
    check(len == 162, "vendor request returns the 162-byte set");
    check(len == 162 && set[0] == 0x0A && set[2] == 0x00 && set[8] == 162,
          "set header");
    check(len == 162 && memcmp(&set[14], "WINUSB\0\0", 8) == 0, "compatible ID WINUSB");
    char guid[64] = {0};
    for(int i = 0; len == 162 && i < 38; i++)
        guid[i] = (char)set[82 + 2 * i];
    check(strcmp(guid, kGuid) == 0, "device interface GUID");

    uint8_t other[8];
    len = libusb_control_transfer(h, 0xC0, (uint8_t)(vendor + 1), 0, 7, other, sizeof(other), 1000);
    check(len < 0, "any other vendor request is refused (stall)");

    libusb_close(h);
    libusb_exit(ctx);
    printf(fails ? "FAIL: %d check(s)\n" : "PASS\n", fails);
    return fails != 0;
}

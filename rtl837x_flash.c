/* RTL837x SPI Flash driver with ZB25D40B (JEDEC 5E 32 13) support */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_sfr.h"

__xdata uint8_t dio_enabled;
__xdata struct flash_region_t flash_region;
__xdata uint32_t flash_size;
__xdata uint8_t flash_capacity_code;

#define CMD_WRITE_STATUS        0x01
#define CMD_PAGE_PROGRAM        0x02
#define CMD_READ_STATUS         0x05
#define CMD_WRITE_ENABLE        0x06
#define CMD_FREAD               0x0b
#define CMD_SECTOR_ERASE        0x20
#define CMD_READ_SECURITY_REGS  0x48
#define CMD_READ_UNIQUE_ID      0x4b
#define CMD_READ_JEDEC_ID       0x9f
#define CMD_FREAD_DIO           0xbb
#define CMD_FREAD_DUAL_OUT      0x3b
#define STATUS_REG_BUSY_MASK    0x01
#define STATUS_REG_WEL_MASK     0x02
#define FLASH_JEDEC_ZB25D40B    0x5e3213UL

static __xdata uint8_t flash_is_zb25d40b;

static void flash_configure_mmio(void)
{
    while(SFR_FLASH_EXEC_BUSY);
    if (dio_enabled) {
        SFR_FLASH_MODEB = 0x18;
        SFR_FLASH_CMD_R = CMD_FREAD_DIO;
        SFR_FLASH_DUMMYCYCLES = 4;
        return;
    }
    SFR_FLASH_MODEB = 0x0;
    SFR_FLASH_CMD_R = CMD_FREAD;
    SFR_FLASH_DUMMYCYCLES = 8;
}

static void flash_configure_sio(void)
{
    while(SFR_FLASH_EXEC_BUSY);
    SFR_FLASH_MODEB = 0x0;
    SFR_FLASH_DUMMYCYCLES = 0;
}

static uint8_t flash_read_status(void)
{
    uint8_t status, old_cmd_r, old_tconf;
    while(SFR_FLASH_EXEC_BUSY);
    old_cmd_r = SFR_FLASH_CMD_R;
    old_tconf = SFR_FLASH_TCONF;
    SFR_FLASH_TCONF = 0x11;
    SFR_FLASH_CMD_R = CMD_READ_STATUS;
    SFR_FLASH_EXEC_GO = 1;
    while(SFR_FLASH_EXEC_BUSY);
    status = SFR_FLASH_DATA0;
    SFR_FLASH_CMD_R = old_cmd_r;
    SFR_FLASH_TCONF = old_tconf;
    return status;
}

static void flash_write_enable(void)
{
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    while(SFR_FLASH_EXEC_BUSY);
    SFR_FLASH_TCONF = 0x18;
    SFR_FLASH_CMD = CMD_WRITE_ENABLE;
    SFR_FLASH_EXEC_GO = 1;
    while (SFR_FLASH_EXEC_BUSY);
    while (!(flash_read_status() & STATUS_REG_WEL_MASK));
}

static uint32_t flash_read_jedecid_raw(void)
{
    uint32_t id;
    flash_configure_sio();
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    SFR_FLASH_CMD_R = CMD_READ_JEDEC_ID;
    SFR_FLASH_TCONF = 0x13;
    SFR_FLASH_EXEC_GO = 1;
    while(SFR_FLASH_EXEC_BUSY);
    id = ((uint32_t)SFR_FLASH_DATA0 << 16) |
         ((uint32_t)SFR_FLASH_DATA8 << 8) |
         (uint32_t)SFR_FLASH_DATA16;
    return id;
}

void flash_init(uint8_t enable_dio)
{
    uint32_t jedec_id;

    /* Always start in safe Single-I/O Fast Read mode. */
    SFR_FLASH_CONFIG = 9;
    SFR_FLASH_CONF_RCMD = CMD_FREAD;
    SFR_FLASH_CONF_DIV = 8;
    while(SFR_FLASH_EXEC_BUSY);
    SFR_FLASH_DUMMYCYCLES = 8;
    SFR_FLASH_MODEB = 0;

    /* Identify the Flash before selecting DIO. */
    jedec_id = flash_read_jedecid_raw();

    if (jedec_id == FLASH_JEDEC_ZB25D40B) {
        /* ZB25D40B: use 0x0B Single-I/O Fast Read, 8 dummy clocks. */
        flash_is_zb25d40b = 1;
        dio_enabled = 0;
        flash_configure_mmio();
        return;
    }

    flash_is_zb25d40b = 0;
    dio_enabled = enable_dio;

    if (enable_dio) {
        SFR_FLASH_CONFIG = 9;
        SFR_FLASH_CONF_RCMD = CMD_FREAD_DIO;
        SFR_FLASH_CONF_DIV = 4;
    } else {
        SFR_FLASH_CONFIG = 9;
        SFR_FLASH_CONF_RCMD = CMD_FREAD;
        SFR_FLASH_CONF_DIV = 8;
    }
    while(SFR_FLASH_EXEC_BUSY);

    /* Preserve original behaviour for other Flash chips, but issue WREN first. */
    flash_configure_sio();
    flash_write_enable();
    SFR_FLASH_DUMMYCYCLES = 8;
    SFR_FLASH_MODEB = 0;
    SFR_FLASH_TCONF = 0x19;
    SFR_FLASH_CMD = CMD_WRITE_STATUS;
    SFR_FLASH_DATA0 = 0;
    SFR_FLASH_EXEC_GO = 1;
    while(SFR_FLASH_EXEC_BUSY);
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    flash_configure_mmio();
}

void flash_read_uid(void)
{
    flash_configure_sio();
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    SFR_FLASH_CMD_R = CMD_READ_UNIQUE_ID;
    SFR_FLASH_DUMMYCYCLES = 8;
    SFR_FLASH_TCONF = 4;
    SFR_FLASH_ADDR16 = 0;
    SFR_FLASH_ADDR8 = 0;
    SFR_FLASH_ADDR0 = 0;
    SFR_FLASH_EXEC_GO = 1;
    while(SFR_FLASH_EXEC_BUSY);
    print_byte(SFR_FLASH_DATA0);
    print_byte(SFR_FLASH_DATA8);
    print_byte(SFR_FLASH_DATA16);
    print_byte(SFR_FLASH_DATA24);
    write_char(' ');
    SFR_FLASH_DUMMYCYCLES = 24;
    SFR_FLASH_EXEC_GO = 1;
    while(SFR_FLASH_EXEC_BUSY);
    print_byte(SFR_FLASH_DATA0);
    print_byte(SFR_FLASH_DATA8);
    print_byte(SFR_FLASH_DATA16);
    print_byte(SFR_FLASH_DATA24);
    flash_configure_mmio();
}

__code const char* get_flash_size_str(void)
{
    switch (flash_capacity_code) {
        case 0x12: return "256 KB";
        case 0x13: return "512 KB";
        case 0x14: return "1 MB";
        case 0x15: return "2 MB";
        case 0x16: return "4 MB";
        case 0x17: return "8 MB";
        case 0x18: return "16 MB";
        default: return "unknown";
    }
}

void flash_read_jedecid(void)
{
    uint8_t manufacturer_id, memory_type, capacity_code;
    uint32_t jedec_id;
    flash_configure_sio();
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    SFR_FLASH_CMD_R = CMD_READ_JEDEC_ID;
    SFR_FLASH_TCONF = 0x13;
    SFR_FLASH_EXEC_GO = 1;
    while(SFR_FLASH_EXEC_BUSY);
    manufacturer_id = SFR_FLASH_DATA0;
    memory_type = SFR_FLASH_DATA8;
    capacity_code = SFR_FLASH_DATA16;
    jedec_id = ((uint32_t)manufacturer_id << 16) |
               ((uint32_t)memory_type << 8) | capacity_code;
    flash_capacity_code = capacity_code;
    flash_size = 1UL << flash_capacity_code;
    if (jedec_id == FLASH_JEDEC_ZB25D40B) {
        flash_is_zb25d40b = 1;
        dio_enabled = 0;
    }
    print_string("Flash information:\n");
    print_string("  Manufacturer ID: 0x"); print_byte(manufacturer_id);
    print_string("\n  Memory Type:     0x"); print_byte(memory_type);
    print_string("\n  Capacity:        0x"); print_byte(capacity_code);
    print_string(" = "); print_string(get_flash_size_str());
    if (jedec_id == FLASH_JEDEC_ZB25D40B) print_string(" [ZB25D40B]");
    write_char('\n');
    flash_configure_mmio();
}

void flash_read_bulk(__xdata uint8_t *dst)
{
    if (!flash_region.len) return;
    flash_configure_sio();
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    flash_configure_mmio();
    while (1) {
        SFR_FLASH_ADDR16 = flash_region.addr >> 16;
        SFR_FLASH_ADDR8 = flash_region.addr >> 8;
        SFR_FLASH_ADDR0 = flash_region.addr;
        flash_region.addr += 4;
        SFR_FLASH_TCONF = 4;
        SFR_FLASH_EXEC_GO = 1;
        while(SFR_FLASH_EXEC_BUSY);
        *dst++ = SFR_FLASH_DATA0;
        if (flash_region.len == 1) break;
        *dst++ = SFR_FLASH_DATA8;
        if (flash_region.len == 2) break;
        *dst++ = SFR_FLASH_DATA16;
        if (flash_region.len == 3) break;
        *dst++ = SFR_FLASH_DATA24;
        if (flash_region.len == 4) break;
        flash_region.len -= 4;
    }
}

void flash_read_security(void)
{
    if (!flash_region.len) return;
    flash_configure_sio();
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    SFR_FLASH_CMD_R = CMD_READ_SECURITY_REGS;
    SFR_FLASH_TCONF = 4;
    do {
        SFR_FLASH_ADDR16 = flash_region.addr >> 16;
        SFR_FLASH_ADDR8 = flash_region.addr >> 8;
        SFR_FLASH_ADDR0 = flash_region.addr;
        flash_region.addr += 4;
        SFR_FLASH_EXEC_GO = 1;
        while(SFR_FLASH_EXEC_BUSY);
        print_byte(SFR_FLASH_DATA0);
        if (flash_region.len == 1) break;
        print_byte(SFR_FLASH_DATA8);
        if (flash_region.len == 2) break;
        print_byte(SFR_FLASH_DATA16);
        if (flash_region.len == 3) break;
        print_byte(SFR_FLASH_DATA24);
        write_char(' ');
        flash_region.len -= 4;
    } while(flash_region.len);
    flash_configure_mmio();
}

void flash_sector_erase(void)
{
    flash_configure_sio();
    flash_write_enable();
    SFR_FLASH_TCONF = 8;
    SFR_FLASH_CMD = CMD_SECTOR_ERASE;
    SFR_FLASH_ADDR16 = flash_region.addr >> 16;
    SFR_FLASH_ADDR8 = flash_region.addr >> 8;
    SFR_FLASH_ADDR0 = flash_region.addr;
    SFR_FLASH_EXEC_GO = 1;
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    flash_configure_mmio();
}

void flash_write_bytes(__xdata uint8_t *ptr)
{
    flash_configure_sio();
    while(1) {
        flash_write_enable();
        SFR_FLASH_CMD = CMD_PAGE_PROGRAM;
        if (flash_region.len < 5)
            SFR_FLASH_TCONF = 8 | flash_region.len;
        else
            SFR_FLASH_TCONF = 0x40 | 8 | 4;
        SFR_FLASH_ADDR16 = flash_region.addr >> 16;
        SFR_FLASH_ADDR8 = flash_region.addr >> 8;
        SFR_FLASH_ADDR0 = flash_region.addr;
        SFR_FLASH_DATA0 = *ptr++;
        if (flash_region.len > 1) SFR_FLASH_DATA8 = *ptr++;
        if (flash_region.len > 2) SFR_FLASH_DATA16 = *ptr++;
        if (flash_region.len > 3) SFR_FLASH_DATA24 = *ptr++;
        SFR_FLASH_EXEC_GO = 1;
        if (flash_region.len < 5) break;
        flash_region.len -= 4;
        flash_region.addr += 4;
    }
    while (flash_read_status() & STATUS_REG_BUSY_MASK);
    flash_configure_mmio();
}

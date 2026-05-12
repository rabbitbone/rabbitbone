#pragma once

#include <aurora/block.h>
#include <aurora/arch/io.h>
#include <aurora/log.h>
#include <aurora/libc.h>
#include <aurora/spinlock.h>

#define ATA_PRIMARY_IO 0x1f0u
#define ATA_PRIMARY_CTRL 0x3f6u
#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_SECCOUNT0 2
#define ATA_REG_LBA0 3
#define ATA_REG_LBA1 4
#define ATA_REG_LBA2 5
#define ATA_REG_HDDEVSEL 6
#define ATA_REG_COMMAND 7
#define ATA_REG_STATUS 7
#define ATA_CMD_IDENTIFY 0xec
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_CACHE_FLUSH 0xe7
#define ATA_CMD_CACHE_FLUSH_EXT 0xea
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_LBA48_LIMIT (1ull << 48)

static block_device_t primary_master;
static u16 identify_data[256];
static bool primary_supports_lba48;
static spinlock_t primary_channel_lock;


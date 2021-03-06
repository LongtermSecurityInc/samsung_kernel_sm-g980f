/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * DP self test
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/dp_logger.h>
#include <linux/device.h>

#include "dp_self_test.h"
#include "../dpu30/displayport.h"

#define WAIT_TIMEOUT_FOR_TEST_START	60000
#define WAIT_TIMEOUT_FOR_RECONNECTION	12000
#define WAIT_TIMEOUT_FOR_NEXT_TEST	6000

struct self_test_vars {
	struct displayport_device *displayport;
	int data_idx;
	int test_on_process;
	enum dex_support_type adapter_type; /* 1: FHD support, 2: WQHD support */

	int waiting;
	wait_queue_head_t test_wait;
};

struct res_timing {
	u32 xres;
	u32 yres;
	u32 fps;
};

struct self_test_data {
	char desc[64];
	u8 edid[256];
	struct res_timing prefer;
	struct res_timing max;
	struct res_timing dex_wqhd;
	struct res_timing dex_fhd;
	u8 prefer_result;
	u8 max_result;
	u8 dex_wqhd_result;
	u8 dex_fhd_result;
	u8 audio_result;
	u8 hdcp_result;
};

static void self_test_process_work(struct work_struct *work);

static DECLARE_WORK(test_work, self_test_process_work);
static struct self_test_vars g_test_vars;

static struct self_test_data test_data[] = {
	{
		"Max 4K 30Hz",
		{
			0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x4c, 0x2d, 0x81, 0x0b, 0x50, 0x4a, 0x4b, 0x30,
			0x14, 0x18, 0x01, 0x03, 0x80, 0x3d, 0x23, 0x78, 0x2a, 0x5f, 0xb1, 0xa2, 0x57, 0x4f, 0xa2, 0x28,
			0x0f, 0x50, 0x54, 0xbf, 0xef, 0x80, 0x71, 0x4f, 0x81, 0x00, 0x81, 0xc0, 0x81, 0x80, 0x95, 0x00,
			0xa9, 0xc0, 0xb3, 0x00, 0xd1, 0x00, 0x04, 0x74, 0x00, 0x30, 0xf2, 0x70, 0x5a, 0x80, 0xb0, 0x58,
			0x8a, 0x00, 0x60, 0x59, 0x21, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x4b, 0x1e,
			0x5a, 0x1e, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x55,
			0x32, 0x38, 0x44, 0x35, 0x39, 0x30, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xff,
			0x00, 0x48, 0x54, 0x50, 0x46, 0x35, 0x30, 0x30, 0x32, 0x33, 0x33, 0x0a, 0x20, 0x20, 0x01, 0x14,
			0x02, 0x03, 0x23, 0xf1, 0x48, 0x90, 0x04, 0x1f, 0x13, 0x03, 0x12, 0x20, 0x22, 0x23, 0x09, 0x07,
			0x07, 0x83, 0x01, 0x00, 0x00, 0x6d, 0x03, 0x0c, 0x00, 0x10, 0x00, 0x80, 0x3c, 0x20, 0x10, 0x60,
			0x01, 0x02, 0x03, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00, 0x60,
			0x59, 0x21, 0x00, 0x00, 0x1e, 0x02, 0x3a, 0x80, 0xd0, 0x72, 0x38, 0x2d, 0x40, 0x10, 0x2c, 0x45,
			0x80, 0x60, 0x59, 0x21, 0x00, 0x00, 0x1e, 0x01, 0x1d, 0x00, 0x72, 0x51, 0xd0, 0x1e, 0x20, 0x6e,
			0x28, 0x55, 0x00, 0x60, 0x59, 0x21, 0x00, 0x00, 0x1e, 0x56, 0x5e, 0x00, 0xa0, 0xa0, 0xa0, 0x29,
			0x50, 0x30, 0x20, 0x35, 0x00, 0x60, 0x59, 0x21, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47
		},
		{3840, 2160, 30},/* prefer */
		{3840, 2160, 30},/* max */
		{2560, 1440, 60},/* dex mode resolution for dexpad*/
		{1920, 1080, 60},/* dex mode resolution for dex cable*/
	},
	{
		"Max 3440x1440 50Hz",
		{
			0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x4c, 0x2d, 0xc3, 0x0d, 0x00, 0x00, 0x00, 0x00,
			0x1b, 0x1a, 0x01, 0x03, 0x80, 0x50, 0x21, 0x78, 0x2a, 0x8d, 0x4c, 0xa9, 0x54, 0x46, 0x97, 0x22,
			0x22, 0x4e, 0x5f, 0xbf, 0xef, 0x80, 0x71, 0x4f, 0x81, 0x00, 0x81, 0xc0, 0x81, 0x80, 0x95, 0x00,
			0xa9, 0xc0, 0xb3, 0x00, 0x01, 0x01, 0x9d, 0x67, 0x70, 0xa0, 0xd0, 0xa0, 0x22, 0x50, 0x30, 0x20,
			0x3a, 0x00, 0x1d, 0x4d, 0x31, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x32, 0x64, 0x1e,
			0x98, 0x37, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x43,
			0x46, 0x37, 0x39, 0x31, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xff,
			0x00, 0x48, 0x31, 0x41, 0x4b, 0x35, 0x30, 0x30, 0x30, 0x30, 0x30, 0x0a, 0x20, 0x20, 0x01, 0xe6,
			0x02, 0x03, 0x25, 0xf1, 0x48, 0x90, 0x1f, 0x04, 0x13, 0x03, 0x12, 0x05, 0x5a, 0x23, 0x09, 0x07,
			0x07, 0x83, 0x01, 0x00, 0x00, 0x67, 0x03, 0x0c, 0x00, 0x20, 0x00, 0x80, 0x3c, 0x67, 0xd8, 0x5d,
			0xc4, 0x01, 0x6e, 0x80, 0x00, 0xe7, 0x7c, 0x70, 0xa0, 0xd0, 0xa0, 0x29, 0x50, 0x30, 0x20, 0x3a,
			0x00, 0x1d, 0x4d, 0x31, 0x00, 0x00, 0x1a, 0x4e, 0xd4, 0x70, 0xa0, 0xd0, 0xa0, 0x46, 0x50, 0x30,
			0x20, 0x3a, 0x00, 0x1d, 0x4d, 0x31, 0x00, 0x00, 0x1a, 0x56, 0x5e, 0x00, 0xa0, 0xa0, 0xa0, 0x29,
			0x50, 0x30, 0x20, 0x35, 0x00, 0x1d, 0x4d, 0x31, 0x00, 0x00, 0x1a, 0x02, 0x3a, 0x80, 0x18, 0x71,
			0x38, 0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x58, 0x4d, 0x00,
			0xb8, 0xa1, 0x38, 0x14, 0x40, 0xf8, 0x2c, 0x45, 0x00, 0x1d, 0x4d, 0x31, 0x00, 0x00, 0x1e, 0x2e
		},
		{3440, 1440, 60},/* prefer, if there is same resolution with differrent fps, then select higher one */
		{3440, 1440, 100},/* max */
		{3440, 1440, 60},/* dex mode resolution for dexpad*/
		{1920, 1080, 60},/* dex mode resolution for dex cable*/
	},
	{
		"Max WHQD 120Hz",
		{
			0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x4c, 0x2d, 0x13, 0x0e, 0x51, 0x50, 0x51, 0x30,
			0x1a, 0x1c, 0x01, 0x03, 0x80, 0x46, 0x27, 0x78, 0x2a, 0xef, 0x35, 0xad, 0x50, 0x44, 0xaa, 0x27,
			0x0f, 0x50, 0x54, 0xbf, 0xef, 0x80, 0x71, 0x4f, 0x81, 0x00, 0x81, 0xc0, 0x81, 0x80, 0x95, 0x00,
			0xa9, 0xc0, 0xb3, 0x00, 0x01, 0x01, 0x6f, 0xc2, 0x00, 0xa0, 0xa0, 0xa0, 0x55, 0x50, 0x30, 0x20,
			0x35, 0x00, 0xb9, 0x88, 0x21, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x78, 0x1e,
			0xb9, 0x3c, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x43,
			0x33, 0x32, 0x48, 0x47, 0x37, 0x78, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xff,
			0x00, 0x48, 0x54, 0x52, 0x4b, 0x36, 0x30, 0x30, 0x30, 0x33, 0x36, 0x0a, 0x20, 0x20, 0x01, 0xd5,
			0x02, 0x03, 0x3f, 0xf1, 0x4e, 0x90, 0x61, 0x1f, 0x04, 0x13, 0x60, 0x5d, 0x5e, 0x20, 0x21, 0x22,
			0x5f, 0x03, 0x12, 0x23, 0x09, 0x07, 0x07, 0x83, 0x01, 0x00, 0x00, 0xe3, 0x05, 0xc0, 0x00, 0x6d,
			0x03, 0x0c, 0x00, 0x10, 0x00, 0xb8, 0x3c, 0x20, 0x00, 0x60, 0x01, 0x02, 0x03, 0x67, 0xd8, 0x5d,
			0xc4, 0x01, 0x78, 0x80, 0x03, 0xe6, 0x06, 0x05, 0x01, 0x73, 0x5a, 0x17, 0xe2, 0x0f, 0x22, 0x02,
			0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00, 0xb9, 0x88, 0x21, 0x00, 0x00,
			0x1e, 0x56, 0x5e, 0x00, 0xa0, 0xa0, 0xa0, 0x29, 0x50, 0x30, 0x20, 0x35, 0x00, 0xb9, 0x88, 0x21,
			0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x72
		},
		{2560, 1440, 120},/* prefer */
		{3840, 2160, 60},/* max */
		{2560, 1440, 60},/* dex mode resolution for dexpad*/
		{1920, 1080, 60},/* dex mode resolution for dex cable*/
	},
	{
		"Prefer 4k 30hz, but Max 4k 60hz",
		{
			0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
			0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04, 0x74, 0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80, 0xB0, 0x58,
			0x8A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x08, 0xE8, 0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80,
			0xB0, 0x58, 0x8A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x51
		},
		{3840, 2160, 60},/* prefer */
		{3840, 2160, 60},/* max */
		{640, 480, 60},/* dex mode. This EDID has no resolution except 4k, so default */
		{640, 480, 60},/* dex mode resolution for dex cable*/
	},
	{
		"4K 60Hz with YCbCr420, 4K 30Hz with RGB888",
		{
			0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x01, 0x03, 0x80, 0x79, 0x44, 0x78, 0x0A, 0x23, 0xAD, 0xA4, 0x54, 0x4D, 0x99, 0x26,
			0x0F, 0x47, 0x4A, 0xBD, 0xEF, 0x80, 0x71, 0x4F, 0x81, 0xC0, 0x81, 0x00, 0x81, 0x80, 0x95, 0x00,
			0xA9, 0xC0, 0xB3, 0x00, 0x01, 0x01, 0x04, 0x74, 0x00, 0x30, 0xF2, 0x70, 0x5A, 0x80, 0xB0, 0x58,
			0x8A, 0x00, 0x50, 0x1D, 0x74, 0x00, 0x00, 0x1E, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40,
			0x58, 0x2C, 0x45, 0x00, 0x50, 0x1D, 0x74, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x18,
			0x78, 0x0F, 0x87, 0x1E, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC,
			0x00, 0x53, 0x41, 0x4D, 0x53, 0x55, 0x4E, 0x47, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x75,
			0x02, 0x03, 0x48, 0xF4, 0x55, 0x5F, 0x10, 0x1F, 0x04, 0x13, 0x05, 0x14, 0x20, 0x21, 0x22, 0x5D,
			0x5E, 0x62, 0x63, 0x64, 0x07, 0x16, 0x03, 0x12, 0x3F, 0x40, 0x23, 0x09, 0x07, 0x07, 0x83, 0x01,
			0x00, 0x00, 0x6E, 0x03, 0x0C, 0x00, 0x10, 0x00, 0xB8, 0x3C, 0x20, 0x00, 0x80, 0x01, 0x02, 0x03,
			0x04, 0xE2, 0x00, 0x4F, 0xE3, 0x05, 0x03, 0x01, 0xE3, 0x06, 0x0D, 0x01, 0xE5, 0x0E, 0x60, 0x61,
			0x65, 0x66, 0xE5, 0x01, 0x8B, 0x84, 0x90, 0x01, 0x01, 0x1D, 0x80, 0xD0, 0x72, 0x1C, 0x16, 0x20,
			0x10, 0x2C, 0x25, 0x80, 0x50, 0x1D, 0x74, 0x00, 0x00, 0x9E, 0x66, 0x21, 0x56, 0xAA, 0x51, 0x00,
			0x1E, 0x30, 0x46, 0x8F, 0x33, 0x00, 0x50, 0x1D, 0x74, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3
		},
		{3840, 2160, 30},/* prefer */
		{3840, 2160, 30},/* max */
		{1920, 1080, 60},/* dex mode resolution for dexpad */
		{1920, 1080, 60},/* dex mode resolution for dex cable */
	},
	{
		"2560*1080@60 on VIC table, Max 3840x1080 60Hz ",
		{
			0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x0E, 0x1D, 0x01, 0x03, 0x80, 0x78, 0x22, 0x78, 0x2A, 0x91, 0xA5, 0xA6, 0x57, 0x52, 0x9C, 0x26,
			0x11, 0x50, 0x54, 0xBF, 0xEF, 0x80, 0x71, 0x4F, 0x81, 0x00, 0x81, 0xC0, 0x81, 0x80, 0x95, 0x00,
			0xA9, 0xC0, 0xB3, 0x00, 0x01, 0x01, 0x1A, 0x68, 0x00, 0xA0, 0xF0, 0x38, 0x1F, 0x40, 0x30, 0x20,
			0x3A, 0x00, 0xAC, 0x50, 0x41, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x32, 0x78, 0x1E,
			0x8C, 0x38, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x43,
			0x34, 0x39, 0x4A, 0x38, 0x39, 0x78, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFF,
			0x00, 0x48, 0x54, 0x4A, 0x4B, 0x34, 0x30, 0x30, 0x30, 0x32, 0x30, 0x0A, 0x20, 0x20, 0x01, 0xB4,
			0x02, 0x03, 0x24, 0xF4, 0x47, 0x90, 0x1F, 0x04, 0x13, 0x03, 0x12, 0x5A, 0x23, 0x09, 0x07, 0x07,
			0x83, 0x01, 0x00, 0x00, 0x67, 0x03, 0x0C, 0x00, 0x10, 0x00, 0x80, 0x3C, 0x67, 0xD8, 0x5D, 0xC4,
			0x01, 0x78, 0x80, 0x00, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C, 0x45, 0x00,
			0xAC, 0x50, 0x41, 0x00, 0x00, 0x1E, 0x74, 0xD6, 0x00, 0xA0, 0xF0, 0x38, 0x40, 0x40, 0x30, 0x20,
			0x3A, 0x00, 0xAC, 0x50, 0x41, 0x00, 0x00, 0x1A, 0xF4, 0xB0, 0x00, 0xA0, 0xF0, 0x38, 0x35, 0x40,
			0x30, 0x20, 0x3A, 0x00, 0xAC, 0x50, 0x41, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCB
		},
		{3840, 1080, 60},/* prefer */
		{3840, 1080, 60},/* max */
		{2560, 1080, 60},/* dex mode resolution for dexpad */
		{1920, 1080, 60},/* dex mode resolution for dex cable*/
	},
	{
		"Prefer:1280x1024, Max 1920x1080",
		{
			0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x01, 0x1E, 0x01, 0x03, 0x80, 0x26, 0x1E, 0x78, 0xEA, 0xC9, 0x75, 0xA6, 0x57, 0x51, 0x9D, 0x27,
			0x0E, 0x50, 0x54, 0x25, 0x09, 0x00, 0x71, 0x4F, 0x81, 0x80, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
			0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 0x2A, 0x00, 0x98, 0x51, 0x00, 0x2A, 0x40, 0x30, 0x70,
			0x13, 0x00, 0x77, 0x2C, 0x11, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x63,
			0x02, 0x03, 0x17, 0x22, 0x4C, 0x90, 0x1F, 0x01, 0x02, 0x03, 0x07, 0x12, 0x16, 0x04, 0x13, 0x14,
			0x05, 0x65, 0x03, 0x0C, 0x00, 0x10, 0x00, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58,
			0x2C, 0x45, 0x00, 0x77, 0x2C, 0x11, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x59
		},
		{1280, 1024, 60},/* prefer */
		{1920, 1080, 60},/* max */
#ifdef FEATURE_IGNORE_PREFER_IF_DEX_RES_EXIST
		{1920, 1080, 60},/* dex mode resolution for dexpad */
		{1920, 1080, 60},/* dex mode resolution for dex cable*/
#else
		{1280, 1024, 60},/* dex mode resolution for dexpad */
		{1280, 1024, 60},/* dex mode resolution for dex cable*/
#endif
	}
};

static void self_test_reconnect(void)
{
	displayport_hpd_changed(0);
}

static long self_test_wait_resolution_update(u32 timeout_ms)
{
	g_test_vars.waiting = 0;
	return wait_event_interruptible_timeout(g_test_vars.test_wait, g_test_vars.waiting,
			msecs_to_jiffies(timeout_ms));
}

static void self_test_data_reset(void)
{
	int data_cnt = ARRAY_SIZE(test_data);
	int i;

	for (i = 0; i < data_cnt; i++) {
		test_data[i].prefer_result = 0;
		test_data[i].max_result = 0;
		test_data[i].dex_wqhd_result = 0;
		test_data[i].dex_fhd_result = 0;
		test_data[i].hdcp_result = 0;
	}
}

static void self_test_process_work(struct work_struct *work)
{
	int data_cnt = ARRAY_SIZE(test_data);
	int old_dp_mode = g_test_vars.displayport->dex_setting;
	long timeout = 0;

	g_test_vars.data_idx = 0;
	g_test_vars.adapter_type = DEX_FHD_SUPPORT;
	self_test_data_reset();

	timeout = self_test_wait_resolution_update(WAIT_TIMEOUT_FOR_TEST_START);
	if (timeout == 0)
		goto exit;
	msleep(WAIT_TIMEOUT_FOR_NEXT_TEST);

	for (; g_test_vars.data_idx < data_cnt && g_test_vars.test_on_process;
			g_test_vars.data_idx++) {
		displayport_info("self test %d/%d\n", g_test_vars.data_idx + 1, data_cnt);
		/* mirroring mode */
		g_test_vars.displayport->dex_setting = 0;
		self_test_reconnect();
		timeout = self_test_wait_resolution_update(WAIT_TIMEOUT_FOR_RECONNECTION);
		if (timeout)
			msleep(WAIT_TIMEOUT_FOR_NEXT_TEST);
		else
			displayport_info("%d: mirror test timeout\n", g_test_vars.data_idx);

		if (g_test_vars.test_on_process == 0)
			break;

		/* dex mode */
		g_test_vars.displayport->dex_setting = 1;

		/* WQHD supported adapter */
		g_test_vars.adapter_type = DEX_WQHD_SUPPORT;
		self_test_reconnect();
		timeout = self_test_wait_resolution_update(WAIT_TIMEOUT_FOR_RECONNECTION);
		if (timeout)
			msleep(WAIT_TIMEOUT_FOR_NEXT_TEST);
		else
			displayport_info("%d: WQHD Dex test timeout\n", g_test_vars.data_idx);

		if (g_test_vars.test_on_process == 0)
			break;

		/* FHD supported adapter */
		g_test_vars.adapter_type = DEX_FHD_SUPPORT;
		self_test_reconnect();
		timeout = self_test_wait_resolution_update(WAIT_TIMEOUT_FOR_RECONNECTION);
		if (timeout)
			msleep(WAIT_TIMEOUT_FOR_NEXT_TEST);
		else
			displayport_info("%d: FHD Dex test timeout\n", g_test_vars.data_idx);

		if (g_test_vars.test_on_process == 0)
			break;
	}

exit:
	g_test_vars.displayport->dex_setting = old_dp_mode;
	g_test_vars.test_on_process = 0;
	self_test_reconnect();
}

enum dex_support_type self_test_get_dp_adapter_type(void)
{
	return g_test_vars.adapter_type;
}

int self_test_on_process(void)
{
	return g_test_vars.test_on_process;
}

int self_test_get_edid(u8 *edid)
{
	memcpy(edid, test_data[g_test_vars.data_idx].edid, 256);
	return edid[0x7e] + 1;
}

void self_test_resolution_update(u32 xres, u32 yres, u32 fps)
{
	displayport_info("%s(%d, %d, %d): %dx%d %d\n", __func__,
			g_test_vars.data_idx, g_test_vars.displayport->dex_setting,
			g_test_vars.adapter_type, xres, yres, fps);

	if (g_test_vars.test_on_process) {
		switch (g_test_vars.displayport->dex_setting) {
		case 0:/* mirroring mode */
			if (test_data[g_test_vars.data_idx].prefer.xres == xres &&
					test_data[g_test_vars.data_idx].prefer.yres == yres &&
					test_data[g_test_vars.data_idx].prefer.fps - 1 <= fps &&
					test_data[g_test_vars.data_idx].prefer.fps + 1 >= fps)
				test_data[g_test_vars.data_idx].prefer_result = 1;
			break;
		case 1:/* dex mode */
			if (g_test_vars.adapter_type == DEX_WQHD_SUPPORT) {
				if (test_data[g_test_vars.data_idx].dex_wqhd.xres == xres &&
						test_data[g_test_vars.data_idx].dex_wqhd.yres == yres &&
						test_data[g_test_vars.data_idx].dex_wqhd.fps - 1 <= fps &&
						test_data[g_test_vars.data_idx].dex_wqhd.fps + 1 >= fps)
					test_data[g_test_vars.data_idx].dex_wqhd_result = 1;
			} else if (g_test_vars.adapter_type == DEX_FHD_SUPPORT) {
				if (test_data[g_test_vars.data_idx].dex_fhd.xres == xres &&
						test_data[g_test_vars.data_idx].dex_fhd.yres == yres &&
						test_data[g_test_vars.data_idx].dex_fhd.fps - 1 <= fps &&
						test_data[g_test_vars.data_idx].dex_fhd.fps + 1 >= fps)
					test_data[g_test_vars.data_idx].dex_fhd_result = 1;
			}
			break;
		};
	}

	g_test_vars.waiting = 1;
	wake_up_interruptible(&g_test_vars.test_wait);
}

void self_test_audio_param_update(u32 ch, u32 fs, u32 bit)
{
}

static ssize_t self_test_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int size;
	int data_cnt = ARRAY_SIZE(test_data);
	int i;

	size = snprintf(buf, PAGE_SIZE,
			"*** Test result ***\nNo\t Mirror   \tDex Pad  \tDex cable\t Description\n");
	for (i = 0; i < data_cnt; i++) {
		size += snprintf(buf + size, PAGE_SIZE - size,
				"%d:\t %s\t\t %s\t\t %s\t\t ( %s )\n",
					i, test_data[i].prefer_result ? "O":"X",
					test_data[i].dex_wqhd_result ? "O":"X",
					test_data[i].dex_fhd_result ? "O":"X",
					test_data[i].desc);
	}

	return size;
}

static ssize_t self_test_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int val[2] = {0,};

	if (strnchr(buf, size, '-')) {
		pr_err("%s range option not allowed\n", __func__);
		return -EINVAL;
	}

	get_options(buf, 2, val);

	if (val[1] == 1) {
		g_test_vars.test_on_process = 1;
		displayport_info("self test start\n");
		schedule_work(&test_work);
	} else {
		g_test_vars.test_on_process = 0;
		cancel_work_sync(&test_work);
		g_test_vars.waiting = 1;
		wake_up_interruptible(&g_test_vars.test_wait);
		displayport_info("self test stop\n");
	}

	return size;
}
static CLASS_ATTR_RW(self_test);

void self_test_init(struct displayport_device *displayport, struct class *dp_class)
{
	int ret = 0;

	ret = class_create_file(dp_class, &class_attr_self_test);
	if (ret)
		displayport_err("failed to create attr_self_test\n");

	g_test_vars.displayport = displayport;
	init_waitqueue_head(&g_test_vars.test_wait);
}

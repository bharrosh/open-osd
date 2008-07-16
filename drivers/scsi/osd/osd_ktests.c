/*
 * osd_ktests.c - An osd_initiator library in-kernel test suite
 *              called by the osd_uld module
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *   Benny Halevy <bhalevy@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <asm/unaligned.h>
#include <scsi/scsi_device.h>

#include <scsi/osd_initiator.h>
#include <scsi/osd_sec.h>
#include <scsi/osd_attributes.h>

#include "osd_ktests.h"
#include "osd_debug.h"

#ifndef __unused
#    define __unused			__attribute__((unused))
#endif

enum {
	K = 1024,
	M = 1024 * K,
	G = 1024 * M,
};

const u64 format_total_capacity = 128 * M;
const osd_id first_par_id = 0x17171717L;
const osd_id first_obj_id = 0x18181818L;
const unsigned BUFF_SIZE = 4 * K;

const int num_partitions = 1;
const int num_objects = 2; /* per partition */

static int test_exec(struct osd_request *or, void *caps,
		     const struct osd_obj_id *obj)
{
	int ret;
	struct osd_sense_info osi;

	osd_sec_init_nosec_doall_caps(caps, obj, false, true);
	ret = osd_finalize_request(or, 0, caps, NULL);
	if (ret)
		return ret;

	ret = osd_execute_request(or);
	osd_req_decode_sense(or, &osi);
	return ret;
}

#define KTEST_START_REQ(osd_dev, or) do { \
	or = osd_start_request(osd_dev, GFP_KERNEL); \
	if (!or) { \
		OSD_ERR("Error @%s:%d: osd_start_request\n", __func__,\
			__LINE__); \
		return -ENOMEM; \
	} \
} while (0)

#define KTEST_EXEC_END(or, obj, g_caps, msg) do { \
	ret = test_exec(or, g_caps, obj); \
	osd_end_request(or); \
	if (ret) { \
		OSD_ERR("Error executing "msg" => %d\n", ret); \
		return ret; \
	} \
	OSD_DEBUG(msg "\n"); \
} while (0)

static int ktest_format(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 g_caps[OSD_CAP_LEN];
	int ret;

	KTEST_START_REQ(osd_dev, or);
	or->timeout *= 10;
	osd_req_format(or, format_total_capacity);
	KTEST_EXEC_END(or, &osd_root_object, g_caps, "format");
	return 0;
}

static int ktest_creat_par(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 g_caps[OSD_CAP_LEN];
	int ret;
	int p;

	for (p = 0; p < num_partitions; p++) {
		struct osd_obj_id par = {
			.partition = first_par_id + p,
			.id = 0
		};

		KTEST_START_REQ(osd_dev, or);
		osd_req_create_partition(or, par.partition);
		KTEST_EXEC_END(or, &par, g_caps, "create_partition");
	}

	return 0;
}

static int ktest_creat_obj(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 g_caps[OSD_CAP_LEN];
	int ret;
	int p, o;

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			KTEST_START_REQ(osd_dev, or);
			osd_req_create_object(or, &obj);
			KTEST_EXEC_END(or, &obj, g_caps, "create_object");
		}

	return 0;
}

static int ktest_write_obj(struct osd_dev *osd_dev, void *write_buff)
{
	struct request_queue *req_q = osd_dev->scsi_device->request_queue;
	struct osd_request *or;
	u8 g_caps[OSD_CAP_LEN];
	int ret;
	int p, o; u64 offset = 0;
	struct bio *write_bio;

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			KTEST_START_REQ(osd_dev, or);
			write_bio = bio_map_kern(req_q, write_buff,
						 BUFF_SIZE, GFP_KERNEL);
			if (!write_bio) {
				OSD_ERR("!!! Failed to allocate write BIO\n");
				return -ENOMEM;
			}

			osd_req_write(or, &obj, write_bio, offset);
			KTEST_EXEC_END(or, &obj, g_caps, "write");
			write_bio = NULL; /* released by scsi_midlayer */
			offset += BUFF_SIZE;
		}

	return 0;
}

static int ktest_read_obj(struct osd_dev *osd_dev, void *write_buff, void *read_buff)
{
	struct request_queue *req_q = osd_dev->scsi_device->request_queue;
	struct osd_request *or;
	u8 g_caps[OSD_CAP_LEN];
	int ret;
	int p, o; u64 offset = 0;
	struct bio *read_bio;

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			KTEST_START_REQ(osd_dev, or);
			read_bio = bio_map_kern(req_q, read_buff,
						BUFF_SIZE, GFP_KERNEL);
			if (!read_bio) {
				OSD_ERR("!!! Failed to allocate read BIO\n");
				return -ENOMEM;
			}

			osd_req_read(or, &obj, read_bio, offset);
			KTEST_EXEC_END(or, &obj, g_caps, "read");
			read_bio = NULL;
			if (memcmp(read_buff, write_buff, BUFF_SIZE))
				OSD_ERR("!!! Read did not compare\n");
			offset += BUFF_SIZE;
		}

	return 0;
}

static int ktest_remove_obj(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 g_caps[OSD_CAP_LEN];
	int ret;
	int p, o;

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			KTEST_START_REQ(osd_dev, or);
			osd_req_remove_object(or, &obj);
			KTEST_EXEC_END(or, &obj, g_caps, "remove_object");
		}

	return 0;
}

static int ktest_remove_par(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 g_caps[OSD_CAP_LEN];
	int ret;
	int p;

	for (p = 0; p < num_partitions; p++) {
		struct osd_obj_id par = {
			.partition = first_par_id + p,
			.id = 0
		};

		KTEST_START_REQ(osd_dev, or);
		osd_req_remove_partition(or, par.partition);
		KTEST_EXEC_END(or, &par, g_caps, "remove_partition");
	}

	return 0;
}

static int ktest_write_read_attr(struct osd_dev *osd_dev, void *buff,
	bool doread, bool doset, bool doget)
{
	struct request_queue *req_q = osd_dev->scsi_device->request_queue;
	struct osd_request *or;
	char g_caps[OSD_CAP_LEN];
	int ret;
	struct bio *bio;
	const char *domsg;
	/* set attrs */
	static char name[] = "ktest_write_read_attr";
	__be64 max_len = cpu_to_be64(0x80000000L);
	struct osd_obj_id obj = {
		.partition = first_par_id,
		.id = first_obj_id,
	};
	struct osd_attr set_attrs[] = {
		ATTR_SET(OSD_APAGE_OBJECT_QUOTAS, OSD_ATTR_OQ_MAXIMUM_LENGTH,
			sizeof(max_len), &max_len),
		ATTR_SET(OSD_APAGE_OBJECT_INFORMATION, OSD_ATTR_OI_USERNAME,
			sizeof(name), name),
	};
	struct osd_attr get_attrs[] = {
		ATTR_DEF(OSD_APAGE_OBJECT_INFORMATION,
			OSD_ATTR_OI_USED_CAPACITY, sizeof(__be64)),
		ATTR_DEF(OSD_APAGE_OBJECT_INFORMATION,
			OSD_ATTR_OI_LOGICAL_LENGTH, sizeof(__be64)),
	};

	KTEST_START_REQ(osd_dev, or);
	bio = bio_map_kern(req_q, buff, BUFF_SIZE, GFP_KERNEL);
	if (!bio) {
		OSD_ERR("!!! Failed to allocate BIO\n");
		return -ENOMEM;
	}

	if (doread) {
		osd_req_read(or, &obj, bio, 0);
		domsg = "Read-with-attr";
	} else {
		osd_req_write(or, &obj, bio, 0);
		domsg = "Write-with-attr";
	}

	if (doset)
		osd_req_add_set_attr_list(or, set_attrs, 2);
	if (doget)
		osd_req_add_get_attr_list(or, get_attrs, 2);

	ret = test_exec(or, g_caps, &obj);
	if (!ret && doget) {
		void *iter = NULL, *pFirst, *pSec;
		int nelem = 2;
		u64 capacity_len = ~0;
		u64 logical_len = ~0;

		osd_req_decode_get_attr_list(or, get_attrs, &nelem, &iter);

		/*FIXME: Std does not guaranty order of return attrs */
		pFirst = get_attrs[0].val_ptr;
		if (pFirst)
			capacity_len = get_unaligned_be64(pFirst);
		else
			OSD_ERR("failed to read capacity_used\n");
		pSec = get_attrs[1].val_ptr;
		if (pSec)
			logical_len = get_unaligned_be64(pSec);
		else
			OSD_ERR("failed to read logical_length\n");
		OSD_INFO("%s capacity=%llu len=%llu\n",
			domsg, _LLU(capacity_len), _LLU(logical_len));
	}

	osd_end_request(or);
	if (ret) {
		OSD_ERR("!!! Error executing %s => %d doset=%d doget=%d\n",
			domsg, ret, doset, doget);
		return ret;
	}
	OSD_DEBUG("%s\n", domsg);

	return 0;
}

int do_test_17(struct osd_dev *od, unsigned cmd __unused,
		unsigned long arg __unused)
{
	void *write_buff = NULL;
	void *read_buff = NULL;
	int ret = -ENOMEM;
	unsigned i;

/* osd_format */
	if (ktest_format(od))
		goto dev_fini;

/* create some partition */
	if (ktest_creat_par(od))
		goto dev_fini;
/* list partition see if they're all there */
/* create some objects on some partitions */
	if (ktest_creat_obj(od))
		goto dev_fini;

/* Alloc some buffers and bios */
/*	write_buff = kmalloc(BUFF_SIZE, or->alloc_flags);*/
/*	read_buff = kmalloc(BUFF_SIZE, or->alloc_flags);*/
	write_buff = (void *)__get_free_page(GFP_KERNEL);
	read_buff = (void *)__get_free_page(GFP_KERNEL);
	if (!write_buff || !read_buff) {
		OSD_ERR("!!! Failed to allocate memory for test\n");
		goto dev_fini;
	}
	for (i = 0; i < BUFF_SIZE / 4; i++)
		((int *)write_buff)[i] = i;
	OSD_DEBUG("allocate buffers\n");

/* write to objects */
	ret = ktest_write_obj(od, write_buff);
	if (ret)
		goto dev_fini;

/* read from objects and compare to write */
	ret = ktest_read_obj(od, write_buff, read_buff);
	if (ret)
		goto dev_fini;

/* List all objects */

/* Write with get_attr */
	ret = ktest_write_read_attr(od, write_buff, false, false, true);
	if (ret)
		goto dev_fini;

/* Write with set_attr */
	ret = ktest_write_read_attr(od, write_buff, false, true, false);
	if (ret)
		goto dev_fini;

/* Write with set_attr + get_attr */
	ret = ktest_write_read_attr(od, write_buff, false, true, true);
	if (ret)
		goto dev_fini;

/* Read with set_attr */
	ret = ktest_write_read_attr(od, write_buff, true, true, false);
	if (ret)
		goto dev_fini;

/* Read with get_attr */
	ret = ktest_write_read_attr(od, write_buff, true, false, true);
	if (ret)
		goto dev_fini;

/* Read with get_attr + set_attr */
	ret = ktest_write_read_attr(od, write_buff, true, true, true);
	if (ret)
		goto dev_fini;

/* remove objects */
	ret = ktest_remove_obj(od);
	if (ret)
		goto dev_fini;

/* remove partitions */
	ret = ktest_remove_par(od);
	if (ret)
		goto dev_fini;

/* Error to test sense handling */
	ret = ktest_write_obj(od, write_buff);
	if (!ret) {
		OSD_INFO("Error was expected written to none existing object\n");
		ret = -EIO;
	} else
		ret =  0; /* good this test should fail */

/* good and done */
	OSD_INFO("test17: All good and done\n");
dev_fini:
	if (read_buff)
		free_page((ulong)read_buff);
	if (write_buff)
		free_page((ulong)write_buff);

	return ret;
}

#ifdef __KERNEL__
static const char *osd_ktests_version = "open-osd osd_ktests 0.1.0";

MODULE_AUTHOR("Boaz Harrosh <bharrosh@panasas.com>");
MODULE_DESCRIPTION("open-osd Kernel tests Driver osd_ktests.ko");
MODULE_LICENSE("GPL");

static int __init osd_uld_init(void)
{
	OSD_INFO("LOADED %s\n", osd_ktests_version);
	osduld_register_test(OSD_TEST_ALL, do_test_17);
	return 0;
}

static void __exit osd_uld_exit(void)
{
	osduld_unregister_test(OSD_TEST_ALL);
	OSD_INFO("UNLOADED %s\n", osd_ktests_version);
}

module_init(osd_uld_init);
module_exit(osd_uld_exit);

#endif

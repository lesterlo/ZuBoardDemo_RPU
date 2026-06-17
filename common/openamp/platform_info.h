/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * Copyright (c) 2017-2024 Xilinx, Inc. and Contributors. All rights reserved.
 * Copyright (c) 2022-2024 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PLATFORM_INFO_H_
#define PLATFORM_INFO_H_

#include <openamp/remoteproc.h>
#include <openamp/rpmsg.h>
#include <openamp/virtio.h>
#include <metal/atomic.h>
#include <metal/log.h>
#include "bspconfig.h"
#include "xparameters.h"
#include "xreg_cortexr5.h"

#if defined __cplusplus
extern "C" {
#endif

#ifndef IPI_IRQ_VECT_ID
#if defined(XPAR_XIPIPSU_0_INT_ID)
#define IPI_IRQ_VECT_ID XPAR_XIPIPSU_0_INT_ID
#elif defined(XPAR_XIPIPSU_0_INTR)
/*
 * SDT BSPs expose the raw GIC SPI number as XPAR_XIPIPSU_0_INTR. The
 * FreeRTOS interrupt API used by libmetal expects the local interrupt vector.
 */
#if XPAR_XIPIPSU_0_INTR >= 32
#define IPI_IRQ_VECT_ID (XPAR_XIPIPSU_0_INTR - 32U)
#else
#define IPI_IRQ_VECT_ID XPAR_XIPIPSU_0_INTR
#endif
#else
#error "No XIPIPSU interrupt ID macro found"
#endif
#endif

#ifndef POLL_BASE_ADDR
#if defined(XPAR_XIPIPSU_0_BASE_ADDRESS)
#define POLL_BASE_ADDR XPAR_XIPIPSU_0_BASE_ADDRESS
#elif defined(XPAR_XIPIPSU_0_BASEADDR)
#define POLL_BASE_ADDR XPAR_XIPIPSU_0_BASEADDR
#else
#error "No XIPIPSU base address macro found"
#endif
#endif

#ifndef IPI_CHN_BITMASK
#define IPI_CHN_BITMASK 0x01000000u
#endif

#if XPAR_CPU_ID == 0
#define ZUDEMO_RPU_CORE_ID 0u
#define ZUDEMO_RPU_SERVICE_NAME "mncos-r5c0-ctrl"
#define ZUDEMO_RPU_LED_MASK 0x02u
#define ZUDEMO_RPU_HEARTBEAT_PERIOD_MS 1000u
#ifndef SHARED_MEM_PA
#define SHARED_MEM_PA 0x09860000UL
#endif
#elif XPAR_CPU_ID == 1
#define ZUDEMO_RPU_CORE_ID 1u
#define ZUDEMO_RPU_SERVICE_NAME "mncos-r5c1-ctrl"
#define ZUDEMO_RPU_LED_MASK 0x01u
#define ZUDEMO_RPU_HEARTBEAT_PERIOD_MS 2000u
#ifndef SHARED_MEM_PA
#define SHARED_MEM_PA 0x09e60000UL
#endif
#else
#error "Unsupported RPU CPU ID"
#endif

#define KICK_DEV_NAME "poll_dev"
#define KICK_BUS_NAME "generic"

#ifndef SHARED_MEM_SIZE
#define SHARED_MEM_SIZE 0x48000UL
#endif

#ifndef SHARED_BUF_OFFSET
#define SHARED_BUF_OFFSET 0x8000UL
#endif

struct remoteproc_priv {
	const char *kick_dev_name;
	const char *kick_dev_bus_name;
	struct metal_device *kick_dev;
	struct metal_io_region *kick_io;
#ifndef RPMSG_NO_IPI
	uint32_t ipi_chn_mask;
	atomic_int ipi_nokick;
#endif
};

int32_t platform_init(int32_t argc, char *argv[], void **platform);

struct rpmsg_device *
platform_create_rpmsg_vdev(void *platform, uint32_t vdev_index,
			   uint32_t role,
			   void (*rst_cb)(struct virtio_device *vdev),
			   rpmsg_ns_bind_cb ns_bind_cb);

int32_t platform_poll(void *platform);

struct rproc_plat_info {
	struct rpmsg_device *rpdev;
	struct remoteproc *rproc;
};

int32_t platform_poll_on_vdev_reset(void *arg);
void platform_release_rpmsg_vdev(struct rpmsg_device *rpdev, void *platform);
void platform_cleanup(void *platform);

#if defined __cplusplus
}
#endif

#endif /* PLATFORM_INFO_H_ */

/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * Copyright (c) 2021 Xilinx, Inc. All rights reserved.
 * Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdarg.h>
#include <stdio.h>
#include <metal/irq.h>
#include <metal/sys.h>
#include "FreeRTOS.h"
#include "task.h"
#include "platform_info.h"
#include "xil_cache.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include "xparameters.h"

static int app_gic_initialize(void)
{
	xPortInstallInterruptHandler(IPI_IRQ_VECT_ID,
				     (Xil_ExceptionHandler)metal_xlnx_irq_isr,
				     (void *)IPI_IRQ_VECT_ID);
	return 0;
}

extern char *get_rsc_trace_info(unsigned int *);
static struct {
	char *c_buf;
	unsigned int c_len;
	unsigned int c_pos;
	unsigned int c_cnt;
} circ;

static void rsc_trace_putchar(char c)
{
	if (circ.c_pos >= circ.c_len)
		circ.c_pos = 0;
	circ.c_buf[circ.c_pos++] = c;
}

static void rsc_trace_logger(enum metal_log_level level,
			     const char *format, ...)
{
	char msg[128];
	char *p;
	int len;
	va_list args;

	len = sprintf(msg, "%lu %u L%u ",
		      (unsigned long)xTaskGetTickCount(), circ.c_cnt, level);
	if (len < 0 || len >= (int)sizeof(msg))
		len = 0;
	circ.c_cnt++;

	va_start(args, format);
	vsnprintf(msg + len, sizeof(msg) - len, format, args);
	va_end(args);

	for (len = 0, p = msg; *p && len < (int)sizeof(msg); ++len, ++p)
		rsc_trace_putchar(*p);
	xil_printf("%s", msg);
}

int init_system(void)
{
	int ret;
	struct metal_init_params metal_param = METAL_INIT_DEFAULTS;

	circ.c_buf = get_rsc_trace_info(&circ.c_len);
	if (circ.c_buf && circ.c_len) {
		metal_param.log_handler = rsc_trace_logger;
		metal_param.log_level = METAL_LOG_DEBUG;
		circ.c_pos = circ.c_cnt = 0;
	}

	metal_init(&metal_param);
	app_gic_initialize();

	ret = metal_xlnx_irq_init();
	if (ret)
		ML_ERR("metal_xlnx_irq_init failed.\r\n");

	return ret;
}

void cleanup_system(void)
{
	metal_finish();

	Xil_DCacheDisable();
	Xil_ICacheDisable();
	Xil_DCacheInvalidate();
	Xil_ICacheInvalidate();
}

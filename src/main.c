/*
 * Copyright 2023 Linaro.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <openamp/open_amp.h>
#include "rpmsg_ivshmem_backend.h"

void main (void)
{
	printf("Hello %s \n", CONFIG_BOARD);
}
/*
 * Copyright (c) 2023, Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_MODULE_NAME	rpmsg_ivshmem_backend
#define LOG_LEVEL CONFIG_IVSHMEM_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/virtualization/ivshmem.h>
#include <zephyr/sys/printk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rpmsg_ivshmem_backend.h"

#define VDEV_STATUS_SIZE	0x400
#define VRING_COUNT		2
#define VRING_ALIGNMENT	4
#define VRING_SIZE		16
#define IVSHMEM_EV_LOOP_STACK_SIZE 8192

K_THREAD_STACK_DEFINE(ivshmem_ev_loop_stack, IVSHMEM_EV_LOOP_STACK_SIZE);
static struct k_thread ivshmem_ev_loop_thread;

static const struct device *ivshmem_dev =
		DEVICE_DT_GET(DT_NODELABEL(ivshmem0));

static metal_phys_addr_t shm_physmap[1];
static struct metal_device shm_device = {
	.name = "ivshmem0",
	.bus = NULL,
	.num_regions = 1,
	{
		{
			.virt       = (void *) 0,
			.physmap    = shm_physmap,
			.size       = 0,
			.page_shift = 0xffffffff,
			.page_mask  = 0xffffffff,
			.mem_flags  = 0,
			.ops        = { NULL },
		},
	},
	.node = { NULL },
	.irq_num = 0,
	.irq_info = NULL
};

static struct virtio_vring_info rvrings[2] = {
	[0] = {
		.info.align = VRING_ALIGNMENT,
	},
	[1] = {
		.info.align = VRING_ALIGNMENT,
	},
};

static struct virtio_device vdev;
static struct rpmsg_virtio_device rvdev;
static struct metal_io_region *io;
static struct virtqueue *vq[2];

#ifdef CONFIG_OPENAMP_MASTER
static struct rpmsg_virtio_shm_pool shpool;
#endif

static uintptr_t shmem;
static size_t shmem_size;
struct rpmsg_endpoint rpmsg_ept;
static rpmsg_ivshmem_ept_cb_t ep_cb;

static unsigned char virtio_get_status(struct virtio_device *vdev)
{
#ifdef CONFIG_OPENAMP_MASTER
	return VIRTIO_CONFIG_STATUS_DRIVER_OK;
#else
	return sys_read8(shmem);
#endif
}

static void virtio_set_status(struct virtio_device *vdev, unsigned char status)
{
	sys_write8(status, shmem);
}

static uint32_t virtio_get_features(struct virtio_device *vdev)
{
	return 1 << VIRTIO_RPMSG_F_NS;
}

static void virtio_set_features(struct virtio_device *vdev,
				uint32_t features)
{
}

static void virtio_notify(struct virtqueue *vq)
{
#ifdef CONFIG_OPENAMP_MASTER
	ivshmem_int_peer(ivshmem_dev, (uint16_t)ivshmem_get_id(ivshmem_dev) + 1, 0);
#else
	ivshmem_int_peer(ivshmem_dev, (uint16_t)ivshmem_get_id(ivshmem_dev) -1, 0);
#endif
}

struct virtio_dispatch dispatch = {
	.get_status = virtio_get_status,
	.set_status = virtio_set_status,
	.get_features = virtio_get_features,
	.set_features = virtio_set_features,
	.notify = virtio_notify,
};

int endpoint_cb(struct rpmsg_endpoint *ept, void *data,
		size_t len, uint32_t src, void *priv)
{
	if(ep_cb) {
		return ep_cb(ept, data, len);
	}

	return RPMSG_SUCCESS;
}

static void rpmsg_service_unbind(struct rpmsg_endpoint *ept)
{
}

#ifdef CONFIG_OPENAMP_MASTER

K_SEM_DEFINE(ept_sem, 0, 1);

void ns_bind_cb(struct rpmsg_device *rdev, const char *name, uint32_t dest)
{
	rpmsg_create_ept(&rpmsg_ept, rdev, name,
		RPMSG_ADDR_ANY, dest,
		endpoint_cb,
		rpmsg_service_unbind);

	k_sem_give(&ept_sem);
}

#endif

static void ivshmem_event_loop_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* k_poll was signaled or not */
	unsigned int poll_signaled;
	/* vector received */
	int ivshmem_vector_rx;
	int ret;

	struct k_poll_signal sig;

	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &sig),
	};

	k_poll_signal_init(&sig);

	ret = ivshmem_register_handler(ivshmem_dev, &sig, 0);

	if (ret < 0) {
		printk("registering handlers must be supported: %d \n", ret);
		k_panic();
	}

	while (1) {
		printk("%s: waiting interrupt from client... \n", __func__);
		ret = k_poll(events, ARRAY_SIZE(events), K_FOREVER);

		k_poll_signal_check(&sig, &poll_signaled, &ivshmem_vector_rx);
		/* get ready for next signal */
		k_poll_signal_reset(&sig);

		/* notify receive Vqueue once cross interrupt is received */
		virtqueue_notification(vq[0]);
	}
}

int init_ivshmem_backend(void)
{
	int status = 0;
	struct metal_device *device;
	struct metal_init_params metal_params = METAL_INIT_DEFAULTS;

	if (!IS_ENABLED(CONFIG_IVSHMEM_DOORBELL)) {
		printf("CONFIG_IVSHMEM_DOORBELL is not enabled \n");
		k_panic();
	}

	shmem_size = ivshmem_get_mem(ivshmem_dev, &shmem) - VDEV_STATUS_SIZE;
	shm_device.regions[0].virt = (void *)(shmem + VDEV_STATUS_SIZE);
	shm_device.regions[0].size = shmem_size;
	shm_physmap[0] = (metal_phys_addr_t)shmem;

	printk("Memory got from ivshmem: %p, size: %d \n", shmem, shmem_size);

	k_thread_create(&ivshmem_ev_loop_thread,
		ivshmem_ev_loop_stack,
		IVSHMEM_EV_LOOP_STACK_SIZE,
		(k_thread_entry_t)ivshmem_event_loop_thread,
		NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);

	status = metal_init(&metal_params);
	if (status != 0) {
		printk("metal_init: failed - error code %d\n", status);
		return status;
	}

	status = metal_register_generic_device(&shm_device);
	if (status != 0) {
		printk("Couldn't register shared memory device: %d\n", status);
		return status;
	}

	status = metal_device_open("generic", "ivshmem0", &device);
	if (status != 0) {
		printk("metal_device_open failed: %d\n", status);
		return status;
	}

	io = metal_device_io_region(device, 0);
	if (io == NULL) {
		printk("metal_device_io_region failed to get region\n");
		return status;
	}

	vq[0] = virtqueue_allocate(VRING_SIZE);
	if (vq[0] == NULL) {
		printk("virtqueue_allocate failed to alloc vq[0]\n");
		return status;
	}
	vq[1] = virtqueue_allocate(VRING_SIZE);
	if (vq[1] == NULL) {
		printk("virtqueue_allocate failed to alloc vq[1]\n");
		return status;
	}

	vdev.vrings_num = VRING_COUNT;
	vdev.func = &dispatch;
	rvrings[0].io = io;
	rvrings[0].info.vaddr = (void *)(shmem + shmem_size - VDEV_STATUS_SIZE);
	rvrings[0].info.num_descs = VRING_SIZE;
	rvrings[0].info.align = VRING_ALIGNMENT;
	rvrings[0].vq = vq[0];

	rvrings[1].io = io;
	rvrings[1].info.vaddr = (void *)(shmem + shmem_size);
	rvrings[1].info.num_descs = VRING_SIZE;
	rvrings[1].info.align = VRING_ALIGNMENT;
	rvrings[1].vq = vq[1];

	vdev.vrings_info = &rvrings[0];

#ifdef CONFIG_OPENAMP_MASTER
	vdev.role = RPMSG_HOST;

	rpmsg_virtio_init_shm_pool(&shpool, (void *)(shmem + VDEV_STATUS_SIZE), shmem_size);
	status = rpmsg_init_vdev(&rvdev, &vdev, ns_bind_cb, io, &shpool);
	if (status != 0) {
		printk("rpmsg_init_vdev failed %d\n", status);
		return status;
	}

	k_sem_take(&ept_sem, K_FOREVER);

#else

	vdev.role = RPMSG_REMOTE;
	status = rpmsg_init_vdev(&rvdev, &vdev, NULL, io, NULL);
	if (status != 0) {
		printk("rpmsg_init_vdev failed %d\n", status);
		return status;
	}

	struct rpmsg_device *rdev = rpmsg_virtio_get_rpmsg_device(&rvdev);

	status = rpmsg_create_ept(&rpmsg_ept, rdev, "k", RPMSG_ADDR_ANY,
			RPMSG_ADDR_ANY, endpoint_cb, rpmsg_service_unbind);
	if (status != 0) {
		printk("rpmsg_create_ept failed %d\n", status);
		return status;
	}
#endif

	return 0;
}

SYS_INIT(init_ivshmem_backend, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);


int rpmsg_ivshmem_register_ep_callback(rpmsg_ivshmem_ept_cb_t callback)
{
	ep_cb = callback;
	return 0;
}
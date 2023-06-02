#ifndef RPMSG_IVHSMEM_BACKEND_H__
#define RPMSG_IVHSMEM_BACKEND_H__

#include <openamp/open_amp.h>
#include <metal/device.h>

typedef int (*rpmsg_ivshmem_ept_cb_t) (struct rpmsg_endpoint *ept, void *data,
		size_t len);

/**
 * @brief register rpmmsg endpoint callback to catch incoming data from RPMSG
 *
 * @return 0 on sucess, otherwise negative error code
 */
int rpmsg_ivshmem_register_ep_callback(rpmsg_ivshmem_ept_cb_t callback);

#endif


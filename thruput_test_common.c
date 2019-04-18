/*
 * 吞吐测试工具的公共层
 *
 * 作者:姜先绪<jiangxianxv@163.com>
 *
 * version:v0.1
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/netdevice.h>

#include "thruput_test_core.h"
#include "thruput_test_common.h"
#include "thruput_test_arch_igb.h"
#include "thruput_test_arch_stmmac.h"



struct thruput_common_ops thruput_common_ops_tbl[] = {
        {THRUPUT_IGB, thruput_igb_ops_init, thruput_igb_rx_que_num_get,
         thruput_igb_tx_que_num_get},
        {THRUPUT_STMMAC, thruput_stmmac_ops_init, thruput_stmmac_rx_que_num_get,
         thruput_stmmac_tx_que_num_get},
};
#define THTUPUT_COMMON_OPS_TBL_NUM (sizeof(thruput_common_ops_tbl) / sizeof(struct thruput_common_ops))

struct thruput_common_port_type_map thruput_common_port_type_map_tbl[] = {
        {THRUPUT_IGB_TYPE_CMD_STR, THRUPUT_IGB},
        {THRUPUT_STMMAC_TYPE_CMD_STR, THRUPUT_STMMAC},
};
#define THTUPUT_COMMON_PORT_TYPE_MAP_TBL_NUM \
    (sizeof(thruput_common_port_type_map_tbl) / sizeof(struct thruput_common_port_type_map))





static void thruput_common_test_send(struct thruput_fwd_ctrl *fwd_ctrl)
{
	struct thruput_ctrl *ctrl;
	struct thruput_buf_info *buf_info;

	ctrl = fwd_ctrl->ctrl;
	buf_info = fwd_ctrl->buf_info;
	ctrl->ops.dev_send(ctrl, buf_info, THRUPUT_BD_NUM);
	ctrl->ops.dev_send_clean(ctrl);
	return;
}

static void thruput_common_test_recv(struct thruput_fwd_ctrl *fwd_ctrl)
{
    int count;
	struct thruput_ctrl *ctrl;
	struct thruput_buf_info *buf_info;

	ctrl = fwd_ctrl->ctrl;
	buf_info = fwd_ctrl->buf_info;
	count = ctrl->ops.dev_recv(ctrl, buf_info);
	return;
}

static void thruput_common_test_pass(struct thruput_fwd_ctrl *fwd_ctrl)
{
	int count;
	struct thruput_ctrl *ctrl;
	struct thruput_buf_info *buf_info;

	ctrl = fwd_ctrl->ctrl;
	buf_info = fwd_ctrl->buf_info;
	count = ctrl->ops.dev_recv(ctrl, buf_info);
	if (count) {
		ctrl->ops.dev_send(ctrl, buf_info, count);
		ctrl->ops.dev_send_clean(ctrl);
	}
	return;
}

int thruput_common_ops_init(struct thruput_ctrl *ctrl)
{
	int i;
	int ret = 0;

	for (i = 0; i < THTUPUT_COMMON_OPS_TBL_NUM; i++) {
		if (thruput_common_ops_tbl[i].port_type == ctrl->port_type) {
			thruput_common_ops_tbl[i].thruput_ops_init(&ctrl->ops);
			break;
		}
	}
	if (i >= THTUPUT_COMMON_OPS_TBL_NUM)
		ret = -1;

	return ret;
}

void thruput_common_rx_que_reinit(struct thruput_fwd_ctrl *fwd_ctrl)
{
	struct thruput_ctrl *ctrl;
	struct thruput_buf_info *buf_info;

	ctrl = fwd_ctrl->ctrl;
	buf_info = fwd_ctrl->buf_info;
	ctrl->ops.dev_rx_que_init(ctrl, buf_info);
	return;
}

int thruput_common_rx_que_num_get
    (struct net_device *dev, enum thruput_port_type port_type) {
	int i;
	int que_num = 0;

	for (i = 0; i < THTUPUT_COMMON_OPS_TBL_NUM; i++) {
		if (thruput_common_ops_tbl[i].port_type == port_type) {
			que_num =
			    thruput_common_ops_tbl[i].
			    thruput_rx_que_num_get(dev);
			break;
		}
	}

	return que_num;
}

int thruput_common_tx_que_num_get
    (struct net_device *dev, enum thruput_port_type port_type) {
	int i;
	int que_num = 0;

	for (i = 0; i < THTUPUT_COMMON_OPS_TBL_NUM; i++) {
		if (thruput_common_ops_tbl[i].port_type == port_type) {
			que_num =
			    thruput_common_ops_tbl[i].
			    thruput_tx_que_num_get(dev);
			break;
		}
	}

	return que_num;
}

enum thruput_port_type thruput_common_get_port_type_by_str
    (const char *port_type_str) {
	int i;
	enum thruput_port_type port_type;

	for (i = 0; i < THTUPUT_COMMON_PORT_TYPE_MAP_TBL_NUM; i++) {
		if (!strcmp
		    (port_type_str,
		     thruput_common_port_type_map_tbl[i].port_type_str)) {
			port_type =
			    thruput_common_port_type_map_tbl[i].port_type;
			break;
		}
	}
	if (i >= THTUPUT_COMMON_PORT_TYPE_MAP_TBL_NUM) {
		port_type = THRUPUT_INVALID;
	}
	return port_type;
}

void *thruput_common_test_func_get(enum thruput_test_type test_type)
{
	void *ret = NULL;

	if (THRUPUT_TX_TEST == test_type) {
		ret = thruput_common_test_send;
	} else if (THRUPUT_RX_TEST == test_type) {
		ret = thruput_common_test_recv;
	} else if (THRUPUT_PASS_TEST == test_type) {
		ret = thruput_common_test_pass;
	} else {
		THRUPUT_PRT("invalid test type: %d.\n", test_type);
	}
	return ret;
}

int thruput_common_task_num_get
    (int tx_que_num, int rx_que_num, enum thruput_test_type test_type) {
	int num = 0;

	if (THRUPUT_TX_TEST == test_type) {
		num = tx_que_num;
	} else if (THRUPUT_RX_TEST == test_type) {
		num = rx_que_num;
	} else if (THRUPUT_PASS_TEST == test_type) {
		num = rx_que_num;
	} else {
		THRUPUT_PRT("invalid test type: %d.\n", test_type);
	}
	return num;
}

void thruput_common_dev_irq_dis(struct thruput_ctrl *ctrl)
{
	ctrl->ops.dev_irq_disable(ctrl);
	return;
}

void thruput_common_dev_napi_stop(struct thruput_ctrl *ctrl)
{
	ctrl->ops.dev_napi_stop(ctrl);
	return;
}

int thruput_common_dev_dma_map(struct thruput_ctrl *ctrl)
{
	return ctrl->ops.dev_dma_map(ctrl);
}

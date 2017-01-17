/*
 * 吞吐测试工具的公共层头文件
 *
 * 作者:姜先绪<jiangxianxv@163.com>
 *
 * version:v0.1
 */

#ifndef __THRUPUT_COMMON_H__
#define __THRUPUT_COMMON_H__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/netdevice.h>

#include "thruput_test_core.h"

struct thruput_common_ops {
	enum thruput_port_type port_type;
	void (*thruput_ops_init) (struct thruput_ops * ops);
	int (*thruput_rx_que_num_get) (struct net_device * dev);
	int (*thruput_tx_que_num_get) (struct net_device * dev);
};

struct thruput_common_port_type_map {
	char *port_type_str;
	enum thruput_port_type port_type;
};

extern int thruput_common_ops_init(struct thruput_ctrl *ctrl);
extern void thruput_common_rx_que_reinit(struct thruput_fwd_ctrl *fwd_ctrl);
extern int thruput_common_rx_que_num_get
    (struct net_device *dev, enum thruput_port_type port_type);
extern int thruput_common_tx_que_num_get
    (struct net_device *dev, enum thruput_port_type port_type);
extern void *thruput_common_test_func_get(enum thruput_test_type test_type);
extern int thruput_common_task_num_get
    (int tx_que_num, int rx_que_num, enum thruput_test_type test_type);
extern enum thruput_port_type thruput_common_get_port_type_by_str
    (const char *port_type_str);
extern void thruput_common_dev_irq_dis(struct thruput_ctrl *ctrl);
extern int thruput_common_dev_dma_map(struct thruput_ctrl *ctrl);

#endif /*__THRUPUT_COMMON_H__*/

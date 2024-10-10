/*
 * 吞吐测试工具的ARCH层头文件
 *
 * version:v0.1
 */

#ifndef __THRUPUT_ARCH_STMMAC_H__
#define __THRUPUT_ARCH_STMMAC_H__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/netdevice.h>

#include "thruput_test_core.h"


extern void thruput_stmmac_ops_init(struct thruput_ops *ops);
extern int thruput_stmmac_rx_que_num_get(struct net_device *netdev);
extern int thruput_stmmac_tx_que_num_get(struct net_device *netdev);
#endif /*__THRUPUT_ARCH_STMMAC_H__*/


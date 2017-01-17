/*
 * 吞吐测试工具的ARCH层头文件
 *
 * 作者:姜先绪<jiangxianxv@163.com>
 *
 * version:v0.1
 */

#ifndef __THRUPUT_ARCH_IGB_H__
#define __THRUPUT_ARCH_IGB_H__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include "../src/igb.h"
#include "../src/igb_vmdq.h"

#include "thruput_test_core.h"

#define THRUPUT_IGB_TX_CMD \
    (E1000_ADVTXD_DTYP_DATA|\
     E1000_ADVTXD_DCMD_DEXT|\
     E1000_ADVTXD_DCMD_IFCS|\
     IGB_TXD_DCMD)

extern void thruput_igb_ops_init(struct thruput_ops *ops);
extern int thruput_igb_rx_que_num_get(struct net_device *netdev);
extern int thruput_igb_tx_que_num_get(struct net_device *netdev);
#endif /*__THRUPUT_ARCH_IGB_H__*/


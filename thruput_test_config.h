
#ifndef __THRUPUT_CONFIG_H__
#define __THRUPUT_CONFIG_H__

#include "thruput_test_core.h"
#include "thruput_test_common.h"

static struct thruput_common_ops thruput_common_ops_tbl[] = {
	{THRUPUT_IGB, thruput_igb_ops_init, thruput_igb_rx_que_num_get,
	 thruput_igb_tx_que_num_get},
};

#define THTUPUT_COMMON_OPS_TBL_NUM (sizeof(thruput_common_ops_tbl) / sizeof(struct thruput_common_ops))

static struct thruput_common_port_type_map thruput_common_port_type_map_tbl[] = {
	{THRUPUT_IGB_TYPE_CMD_STR, THRUPUT_IGB},
};

#define THTUPUT_COMMON_PORT_TYPE_MAP_TBL_NUM \
    (sizeof(thruput_common_port_type_map_tbl) / sizeof(struct thruput_common_port_type_map))

#endif /*__THRUPUT_CONFIG_H__*/

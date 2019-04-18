/*
 * ���ļ���һ������intel PCIE��̫����������linuxƽ̨CPU IO���������Թ��ߣ���Ҫ������intelϵ��PCIE�������в���
 * ֧�����ڵķ��Ͳ��ԣ����ղ��ԣ����շ�ͬʱ���е����²���
 * ʹ�÷���:
 * 1������ϵͳ�м���intel��������������ȷ��������
 * 2�����ر�����thruput.ko����shell������ͨ������򿪲�ͬ�Ĳ��Թ���
 *
 * ����(��eth0���������Ͳ����Է���64�ֽھ�����MAC����60�ֽڣ���·�ϻ��ڼ���4�ֽڵ�CRC):
 * ���Ͳ���: echo "start eth0 tx 60" > /proc/thruput_test
 * ���ղ���: echo "start eth0 rx" > /proc/thruput_test
 * ���²���: echo "start eth0 pass" > /proc/thruput_test
 * ֹͣ����: echo "stop" > /proc/thruput_test
 * �鿴ͳ��: echo "view eth0 statis" > /proc/thruput_test
 * ���ͳ��: echo "clean eth0 statis" > /proc/thruput_test
 *
 * ��ע: ֹͣ���������ֹͣ�����Ѿ���ʼ���Ե�����
 *
 * ����:������<jiangxianxv@163.com>
 *
 * version:v0.1
 */

#ifndef __THRUPUT_TEST_CORE_H__
#define __THRUPUT_TEST_CORE_H__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>

#define THRUPUT_BD_NUM (512)
#define THRUPUT_BUF_LEN (2048)
#define THRUPUT_TEST_PKT_LEN (60)
#define THRUPUT_TEST_L2HEAD_LEN (14)
#define THRUPUT_QUE_NUM (8)
#define THRUPUT_SUPOT_MAX_IF_NUM (16)

#define THRUPUT_PRT(fmt,arg...) \
do {\
        printk("[%s,%d]:"fmt, __FUNCTION__, __LINE__, ##arg);\
} while(0)

#define THRUPUT_TRACE(fmt,arg...) \
do {\
        printk("[%s,%d]:"fmt"\n", __FUNCTION__, __LINE__, ##arg);\
} while(0)

#define THRUPUT_IGB_TYPE_CMD_STR ("igb")
#define THRUPUT_IXGE_TYPE_CMD_STR ("ixge")
#define THRUPUT_STMMAC_TYPE_CMD_STR ("stmmac")

enum thruput_port_type {
	THRUPUT_IGB = 1000,
	THRUPUT_IXGE,
	THRUPUT_STMMAC,
	THRUPUT_INVALID,
};

enum thruput_test_type {
	THRUPUT_TX_TEST = 10,
	THRUPUT_RX_TEST,
	THRUPUT_PASS_TEST,
	THRUPUT_INVALID_TEST,
};

struct thruput_dma_buf {
	char buf[2048];
};

struct thruput_desc_buf {
	int len;
	dma_addr_t dma;
};

struct thruput_buf_info {
	struct thruput_desc_buf *rx_buf_curr;
	struct thruput_desc_buf *rx_buf_fill_hw;
	struct thruput_desc_buf tx_buf[THRUPUT_BD_NUM];
	struct thruput_desc_buf rx_buf1[THRUPUT_BD_NUM];
	struct thruput_desc_buf rx_buf2[THRUPUT_BD_NUM];
	struct thruput_dma_buf dma_buf1[THRUPUT_BD_NUM];
	struct thruput_dma_buf dma_buf2[THRUPUT_BD_NUM];
};

struct thruput_ctrl;
struct thruput_ops {
	void (*dev_send) (struct thruput_ctrl * ctrl,
			  struct thruput_buf_info * buf_info, int send_cnt);
	void (*dev_send_clean) (struct thruput_ctrl * ctrl);
	int (*dev_recv) (struct thruput_ctrl * ctrl,
			 struct thruput_buf_info * buf_info);
	void (*dev_rx_que_init) (struct thruput_ctrl * ctrl,
				 struct thruput_buf_info * buf_info);
	void (*dev_irq_disable) (struct thruput_ctrl * ctrl);
	void (*dev_napi_stop) (struct thruput_ctrl * ctrl);
	int (*dev_dma_map) (struct thruput_ctrl * ctrl);
	int (*dev_rx_que_num_get) (struct net_device * netdev);
	int (*dev_tx_que_num_get) (struct net_device * netdev);
};

struct thruput_pkts_statistic {
	u64 rx_que_pkts[THRUPUT_QUE_NUM];
	u64 tx_que_pkts[THRUPUT_QUE_NUM];
	u64 rx_pkts_distribut[THRUPUT_BD_NUM];
	u64 tx_pkts_distribut[THRUPUT_BD_NUM];
};

struct thruput_statistic {
	char name[IFNAMSIZ];
	struct list_head list;
	struct thruput_pkts_statistic pkts_statistic;
};

struct thruput_ctrl {
	int task_num;
	atomic_t refcnt;
	struct net_device *netdev;
	struct thruput_statistic *statistic;
	struct thruput_ops ops;
	enum thruput_port_type port_type;
	enum thruput_test_type test_type;
	struct thruput_buf_info buf_info[];	/*��Ҫ����rx_que_num�� */
};

struct thruput_tsk_arg {
	int task_num;
	struct thruput_ctrl *ctrl;
};

struct thruput_start_arg {
	int tx_pkt_len;
	char name[IFNAMSIZ];
	enum thruput_test_type test_type;
	enum thruput_port_type port_type;
};

struct thruput_fwd_ctrl {
	struct thruput_ctrl *ctrl;
	struct thruput_buf_info *buf_info;
};

static inline void thruput_ctrl_hold(struct thruput_ctrl *ctrl)
{
	atomic_inc(&ctrl->refcnt);
	return;
}

static inline void thruput_ctrl_put(struct thruput_ctrl *ctrl)
{
	atomic_dec(&ctrl->refcnt);
	return;
}

static inline bool thruput_ctrl_free_safe(struct thruput_ctrl *ctrl)
{
	return (0 == atomic_read(&ctrl->refcnt));
}

static inline void thruput_send_log
    (int count, int que_idx, struct thruput_statistic *statis) {
	statis->pkts_statistic.tx_que_pkts[que_idx] += count;
	statis->pkts_statistic.tx_pkts_distribut[count]++;
	return;
}

static inline void thruput_recv_log
    (int count, int que_idx, struct thruput_statistic *statis) {
	statis->pkts_statistic.rx_que_pkts[que_idx] += count;
	statis->pkts_statistic.rx_pkts_distribut[count]++;
	return;
}

#endif /*__THRUPUT_TEST_CORE_H__*/

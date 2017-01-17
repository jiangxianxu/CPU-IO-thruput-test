/*
 * 本文件是一个基于intel PCIE以太网卡驱动的linux平台CPU IO吞吐量测试工具，需要借助于intel系列PCIE网卡进行测试
 * 支持网口的发送测试，接收测试，和收发同时进行的吞吐测试
 * 使用方法:
 * 1、先在系统中加载intel的网卡驱动并正确创建网口
 * 2、加载本工具thruput.ko后在shell环境里通过命令打开不同的测试功能
 *
 * 命令(以igb驱动创建的eth0发送64字节举例，MAC发送60字节，线路上还要再加上4字节的CRC)
 * 发送测试: echo "start igb eth0 tx 60" > /proc/thruput_test
 * 接收测试: echo "start igb eth0 rx" > /proc/thruput_test
 * 吞吐测试: echo "start igb eth0 pass" > /proc/thruput_test
 * 停止测试: echo "stop" > /proc/thruput_test
 * 查看统计: echo "view eth0 statis" > /proc/thruput_test
 * 清除统计: echo "clean eth0 statis" > /proc/thruput_test
 *
 * 备注: 停止测试命令会停止所有已经开始测试的网口
 *
 * 作者:姜先绪<jiangxianxv@163.com>
 *
 * version:v0.1
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>

#include "thruput_test_core.h"
#include "thruput_test_common.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JiangXianxu");

static ssize_t thruput_core_proc_read
(
	struct file *file,
	char __user *buf,
	size_t size,
	loff_t *ppos
);
static ssize_t thruput_core_proc_write
(
	struct file *file,
	const char __user *buffer,
	size_t count,
	loff_t *data
);

static DEFINE_MUTEX(thruput_core_test_mutex);
static bool thruput_core_test_stop_flag = false;
static struct list_head thruput_core_statistic_list;
static char thruput_core_eth_head[THRUPUT_TEST_L2HEAD_LEN] = {
				0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
				0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
				0x08, 0x00
};
static const struct file_operations thruput_core_proc_func = {
	.read		= thruput_core_proc_read,
    .write		= thruput_core_proc_write,
};

static inline void thruput_core_set_stop_flag(bool flag)
{
	thruput_core_test_stop_flag = flag;
    return;
}

static inline bool thruput_core_get_stop_flag(void)
{
    return thruput_core_test_stop_flag;
}

static void thruput_core_statis_str_add_to_list(struct thruput_statistic *statis)
{
	mutex_lock(&thruput_core_test_mutex);
    list_add_tail(&(statis->list), &thruput_core_statistic_list);
    mutex_unlock(&thruput_core_test_mutex);
    return;
}

static void thruput_core_free_statis_list(void)
{
	struct list_head *curr, *next;
    struct thruput_statistic *statistic;
    
	mutex_lock(&thruput_core_test_mutex);
    list_for_each_safe(curr, next, &thruput_core_statistic_list) {
        list_del(curr);
		statistic = list_entry(curr, struct thruput_statistic, list);
        kfree(statistic);
	}
    mutex_unlock(&thruput_core_test_mutex);
    return;
}

static struct thruput_statistic *thruput_core_statis_str_get_by_name(const char *name)
{
    struct list_head *p;
    struct thruput_statistic *statistic;

    mutex_lock(&thruput_core_test_mutex);
    list_for_each(p, &thruput_core_statistic_list) {
        statistic = list_entry(p, struct thruput_statistic, list);
        if (!strcmp(statistic->name, name))
            break;
    }
    mutex_unlock(&thruput_core_test_mutex);
    
    if (p == &thruput_core_statistic_list) {
        statistic = NULL;
    }
    return statistic;
}

struct thruput_statistic *thruput_core_statis_str_alloc(const char *name)
{
	struct list_head *curr, *next;
	struct thruput_statistic *statistic;

    mutex_lock(&thruput_core_test_mutex);
    list_for_each_safe(curr, next, &thruput_core_statistic_list) {
		statistic = list_entry(curr, struct thruput_statistic, list);
		if (!strcmp(statistic->name, name)) {
            list_del(curr);
			break;
        }
	}
    mutex_unlock(&thruput_core_test_mutex);
    
    if (curr == &thruput_core_statistic_list) {
		statistic = kmalloc(sizeof(struct thruput_statistic), GFP_KERNEL);
        if (!statistic)
			THRUPUT_PRT("statistic alloc failed.\n");
    }
    return statistic;
}


static int thruput_core_buf_info_init(struct thruput_ctrl *ctrl, int buf_length)
{
	int i, j;
	struct thruput_buf *buf;
	struct thruput_buf_info *buf_info;

	if (thruput_common_dev_dma_map(ctrl)) {
		THRUPUT_PRT("%s dma map failed.\n", ctrl->netdev->name);
		return (-1);
	}

	for (i = 0; i < ctrl->task_num; i++) {
	    buf_info = &(ctrl->buf_info[i]);
	    for (j = 0; j < THRUPUT_BD_NUM; j++) {
	        buf = &(buf_info->buf[j]);
	        buf->len = buf_length;
            memcpy(buf->buf, thruput_core_eth_head, THRUPUT_TEST_L2HEAD_LEN);
	    }
	}
	return 0;
}

static int thruput_core_test_task(void* arg)
{
	struct thruput_ctrl *ctrl;
	struct thruput_tsk_arg *tsk_arg;
	void (*thruput_test_func)(struct thruput_fwd_ctrl *);
	struct thruput_fwd_ctrl fwd_ctrl;

	tsk_arg = (struct thruput_tsk_arg *)arg;
	ctrl = tsk_arg->ctrl;
	fwd_ctrl.ctrl = ctrl;
	fwd_ctrl.buf_info = &(ctrl->buf_info[tsk_arg->task_num]);
    kfree(tsk_arg);
	thruput_test_func = thruput_common_test_func_get(ctrl->test_type);
	if (!thruput_test_func) {
		THRUPUT_PRT("get test_func failed.\n");
	    goto exit;
	}
    if (THRUPUT_TX_TEST != ctrl->test_type)
    	thruput_common_rx_que_reinit(&fwd_ctrl);
    
	while (true) {
	    thruput_test_func(&fwd_ctrl);
	    if (thruput_core_get_stop_flag()) {
	        break;
	    }
	}

exit:
	thruput_ctrl_put(ctrl);
	if (thruput_ctrl_free_safe(ctrl)) {
		kfree(ctrl);
	}
	return 0;
}

static void thruput_core_test_start(struct thruput_start_arg *arg)
{
	int i;
	int task_num;
	int rx_que_num;
	int tx_que_num;
	int ctrl_size;
	struct thruput_ctrl *ctrl;
	struct task_struct *pass_through_task;
	struct net_device *netdev;
    struct thruput_tsk_arg *tsk_arg;
    struct thruput_statistic *statistic;

	netdev = dev_get_by_name(&init_net, arg->name);
    if (!netdev) {
    	THRUPUT_PRT("get %s netdev failed.\n", arg->name);
        return;
    }
    dev_put(netdev);
    netif_carrier_off(netdev);
    netif_tx_stop_all_queues(netdev);
    thruput_core_set_stop_flag(false);
    
	rx_que_num = thruput_common_rx_que_num_get(netdev, arg->port_type);
    tx_que_num = thruput_common_rx_que_num_get(netdev, arg->port_type);
    task_num = thruput_common_task_num_get(tx_que_num, rx_que_num, arg->test_type);
    ctrl_size = sizeof(struct thruput_ctrl) + (sizeof(struct thruput_buf_info) * task_num);
	ctrl = kmalloc(ctrl_size, GFP_KERNEL | GFP_DMA);
    if (!ctrl) {
		THRUPUT_PRT("%s test alloc ctrl failed.\n", netdev->name);
        return;
    }
    statistic = thruput_core_statis_str_alloc(netdev->name);
    if (!statistic) {
		THRUPUT_PRT("%s alloc statistic str failed.\n", netdev->name);
        kfree(ctrl);
		return;
    }
    memset(ctrl, 0, ctrl_size);
    memset(statistic, 0, sizeof(struct thruput_statistic));
    strncpy(statistic->name, netdev->name, IFNAMSIZ);
    ctrl->statistic = statistic;
    ctrl->netdev = netdev;
    ctrl->task_num = task_num;
    ctrl->test_type = arg->test_type;
    ctrl->port_type = arg->port_type;
    if (thruput_common_ops_init(ctrl)) {
        THRUPUT_PRT("%s init ops failed.\n", netdev->name);
        kfree(ctrl);
		return;
    }

    if (thruput_core_buf_info_init(ctrl, arg->tx_pkt_len)) {
		THRUPUT_PRT("%s dma map failed.\n", netdev->name);
        kfree(ctrl);
		return;
    }

    thruput_common_dev_irq_dis(ctrl);
	thruput_core_statis_str_add_to_list(statistic);
	for (i = 0; i < task_num; i++) {
        tsk_arg = kmalloc(sizeof(struct thruput_tsk_arg), GFP_KERNEL);
        if (!tsk_arg) {
			THRUPUT_PRT("%s[%d] tsk_arg alloc failed.\n", netdev->name, i);
            if (thruput_ctrl_free_safe(ctrl))
				kfree(ctrl);
            return;
        }
        tsk_arg->ctrl = ctrl;
        tsk_arg->task_num = i;
        thruput_ctrl_hold(ctrl);
		pass_through_task = kthread_create(thruput_core_test_task, tsk_arg, "%s_tset[%d]", netdev->name, i);
    	kthread_bind(pass_through_task, i + 1);
    	wake_up_process(pass_through_task);
    }

    return;
}

static int thruput_core_test_stop(void)
{
    thruput_core_set_stop_flag(true);
    return 0;
}

static void thruput_core_view_statistsc(const char *name)
{
	int i;
    u64 rx_pkts = 0;
    u64 tx_pkts = 0;
    struct thruput_statistic *statistic;

	statistic = thruput_core_statis_str_get_by_name(name);
    if (!statistic) {
		THRUPUT_PRT("get %s statistic str failed.\n", name);
        return;
    }
    printk("\nthe %s pkts statistic:\n", name);
	for (i = 0; i < THRUPUT_QUE_NUM; i++) {
    	rx_pkts += statistic->pkts_statistic.rx_que_pkts[i];
        tx_pkts += statistic->pkts_statistic.tx_que_pkts[i];
        printk("rx_que[%d] pkts:%llu \t tx_que[%d] pkts:%llu\n",
            i, statistic->pkts_statistic.rx_que_pkts[i],
            i, statistic->pkts_statistic.tx_que_pkts[i]);
    }
    printk("total rx_pkts:%llu \t total tx_pkts:%llu\n", rx_pkts, tx_pkts);

    printk("\n rx distribut:");
    for (i = 0; i < THRUPUT_BD_NUM; i++) {
    	if (!(i % 20)) {
            printk("\n[%03d]: ", i);
        }
        printk("%llu ", statistic->pkts_statistic.rx_pkts_distribut[i]);
    }
    printk("\n tx distribut:");
    for (i = 0; i < THRUPUT_BD_NUM; i++) {
    	if (!(i % 20)) {
            printk("\n[%03d]: ", i);
        }
        printk("%llu ", statistic->pkts_statistic.tx_pkts_distribut[i]);
    }
    return;
}

static void thruput_core_clean_statistsc(const char *name)
{
    struct thruput_statistic *statistic;

	statistic = thruput_core_statis_str_get_by_name(name);
    if (!statistic) {
		THRUPUT_PRT("get %s statistic str failed.\n", name);
        return;
    }
	memset(&(statistic->pkts_statistic), 0, sizeof(struct thruput_pkts_statistic));
    return;
}

static ssize_t thruput_core_proc_read
(
	struct file *file,
	char __user *buf,
	size_t size,
	loff_t *ppos
)
{
    return 0;
}

static enum thruput_test_type thruput_core_get_test_type_by_str(const char *str)
{
	enum thruput_test_type ret;
    
	if (!strcmp(str, "tx"))
		ret = THRUPUT_TX_TEST;
    else if (!strcmp(str, "rx"))
		ret = THRUPUT_RX_TEST;
    else if (!strcmp(str, "pass"))
    	ret = THRUPUT_PASS_TEST;
    else
        ret = THRUPUT_INVALID_TEST;
    return ret;
}

static ssize_t thruput_core_proc_write
(
	struct file *file,
	const char __user *buffer,
	size_t count,
	loff_t *data
)
{
    s32 i, j = 0, k = 0;
    char *argv[10] ;
    char tmp[10][96];
    struct thruput_start_arg start_arg;

    if (!count) {
        return 0;
    }
    
    if (count > 96) {
        count = 96;
    }
    for(i = 0; i < 10; i++) {
        argv[i] = tmp[i];
    }
    memset(tmp, 0, sizeof(tmp));
    for (i = 0; i < (count - 1); i++) {
        if (buffer[i] == ' ') {
            argv[j][k] = '\0';
            k = 0;
            j++;
        } else {
            argv[j][k] = buffer[i];
            k++;
        }
    }

    if (0 == strcmp(argv[0], "start"))
    {
    	start_arg.port_type = thruput_common_get_port_type_by_str(argv[1]);
        if (THRUPUT_INVALID == start_arg.port_type)
			goto help;
        
        start_arg.test_type = thruput_core_get_test_type_by_str(argv[3]);
        if (THRUPUT_INVALID_TEST == start_arg.test_type)
            goto help;

        if (THRUPUT_TX_TEST == start_arg.test_type)
        	start_arg.tx_pkt_len = simple_strtoul(argv[4], NULL, 10);
        else
            start_arg.tx_pkt_len = THRUPUT_TEST_PKT_LEN;
        
        strncpy(start_arg.name, argv[2], IFNAMSIZ);
        thruput_core_test_start(&start_arg);
    }
    else if (0 == strcmp(argv[0], "stop"))
    {
        thruput_core_test_stop();
    }
    else if (0 == strcmp(argv[0], "view"))
    {
    	if (0 == strcmp(argv[2], "statis"))
    		thruput_core_view_statistsc(argv[1]);
        else
        	goto help;
    }
    else if (0 == strcmp(argv[0], "clean"))
    {
    	if (0 == strcmp(argv[2], "statis"))
    		thruput_core_clean_statistsc(argv[1]);
        else
        	goto help;
    }
    else
    {
        goto help;
    }
    return count;

help:
    printk("thruput test cmd help:\n");
    printk("start the eth0 thruput test cmd: echo \"start [igb | ixge] eth0 [tx [lenght] | rx | pass]\" > /proc/thruput_test \n");
    printk("stop the all thruput test cmd: echo \"stop\" > /proc/thruput_test \n"); 
    printk("view the eth0 statistic: echo \"view eth0 statis\" > /proc/thruput_test \n"); 
    printk("view the eth0 statistic: echo \"clean eth0 statis\" > /proc/thruput_test \n"); 
    return count;
}

static void thruput_core_proc_initialize(void)
{
    struct proc_dir_entry *entry;

    entry = proc_create_data("thruput_test", 0, NULL, &thruput_core_proc_func, NULL);    
    if (entry == NULL) {
        THRUPUT_PRT("thruput_test proc failed\n");
    }
    return;
}

static void thruput_core_proc_shutdown(void)
{
    remove_proc_entry("thruput_test", NULL);
    return;
}

static int __init thruput_core_init_module(void)
{
    INIT_LIST_HEAD(&thruput_core_statistic_list);
	thruput_core_proc_initialize();
    return 0;
}

static __exit void thruput_core_exit_module(void)
{
	thruput_core_test_stop();
	thruput_core_free_statis_list();
	thruput_core_proc_shutdown();
    return;
}

module_init(thruput_core_init_module);
module_exit(thruput_core_exit_module);



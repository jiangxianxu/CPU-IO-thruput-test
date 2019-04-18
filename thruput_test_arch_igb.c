/*
 * 吞吐测试工具的IGB驱动ARCH层
 * 说明：其中关闭中断和寄存器读写代码来源于intel的IGB驱动
 * 作者:姜先绪<jiangxianxv@163.com>
 *
 * version:v0.1
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>

#include "../src/igb.h"
#include "../src/igb_vmdq.h"

#include "thruput_test_core.h"
#include "thruput_test_arch_igb.h"

u32 e1000_read_reg(struct e1000_hw *hw, u32 reg)
{
	struct igb_adapter *igb = container_of(hw, struct igb_adapter, hw);
	u8 __iomem *hw_addr = ACCESS_ONCE(hw->hw_addr);
	u32 value = 0;

	if (E1000_REMOVED(hw_addr))
		return ~value;

	value = readl(&hw_addr[reg]);

	/* reads should not return all F's */
	if (!(~value) && (!reg || !(~readl(hw_addr)))) {
		struct net_device *netdev = igb->netdev;

		hw->hw_addr = NULL;
		netif_device_detach(netdev);
		netdev_err(netdev, "PCIe link lost, device now detached\n");
	}

	return value;
}

/**
 * igb_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/
static void igb_irq_disable(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	/*
	 * we need to be careful when disabling interrupts.  The VFs are also
	 * mapped into these registers and so clearing the bits can cause
	 * issues on the VF drivers so we only need to clear what we set
	 */
	if (adapter->msix_entries) {
		u32 regval = E1000_READ_REG(hw, E1000_EIAM);

		E1000_WRITE_REG(hw, E1000_EIAM, regval
				& ~adapter->eims_enable_mask);
		E1000_WRITE_REG(hw, E1000_EIMC, adapter->eims_enable_mask);
		regval = E1000_READ_REG(hw, E1000_EIAC);
		E1000_WRITE_REG(hw, E1000_EIAC, regval
				& ~adapter->eims_enable_mask);
	}

	E1000_WRITE_REG(hw, E1000_IAM, 0);
	E1000_WRITE_REG(hw, E1000_IMC, ~0);
	E1000_WRITE_FLUSH(hw);

	if (adapter->msix_entries) {
		int vector = 0, i;

		synchronize_irq(adapter->msix_entries[vector++].vector);

		for (i = 0; i < adapter->num_q_vectors; i++)
			synchronize_irq(adapter->msix_entries[vector++].vector);
	} else {
		synchronize_irq(adapter->pdev->irq);
	}
	return;
}

static void thruput_igb_irq_disable(struct thruput_ctrl *ctrl)
{
	igb_irq_disable(netdev_priv(ctrl->netdev));
	return;
}

static void thruput_igb_napi_stop(struct thruput_ctrl *ctrl)
{
	int i;
	struct igb_adapter *adapter;

	adapter = netdev_priv(ctrl->netdev);
	for (i = 0; i < adapter->num_q_vectors; i++) {
		if (adapter->q_vector[i]) {
			printk("%s %d\n", __FUNCTION__, __LINE__);
			napi_synchronize(&adapter->q_vector[i]->napi);
			napi_disable(&adapter->q_vector[i]->napi);
		}
	}
	del_timer_sync(&adapter->watchdog_timer);
	if (adapter->flags & IGB_FLAG_DETECT_BAD_DMA)
		del_timer_sync(&adapter->dma_err_timer);
	del_timer_sync(&adapter->phy_info_timer);

	printk("%s %d\n", __FUNCTION__, __LINE__);
	return;
}

static void thruput_igb_send
    (struct thruput_ctrl *ctrl,
     struct thruput_buf_info *buf_info, int send_cnt) {
	int count = 0;
	u16 send_index;
	u16 send_index_next;
	u32 cmd_type = THRUPUT_IGB_TX_CMD;
	struct igb_ring *tx_ring;
	struct net_device *netdev;
	struct igb_adapter *adapter;
	union e1000_adv_tx_desc *tx_desc;
	struct thruput_desc_buf *tx_buf;

	netdev = ctrl->netdev;
	adapter = netdev_priv(netdev);
	tx_ring = adapter->tx_ring[0];
	send_index = tx_ring->next_to_use;
	tx_buf = buf_info->tx_buf;
	while (true) {
		send_index_next = send_index + 1;
		if (send_index_next >= tx_ring->count) {
			send_index_next = 0;
		}

		if (send_index_next == tx_ring->next_to_clean) {
			break;
		}

		tx_desc = IGB_TX_DESC(tx_ring, send_index);
		tx_desc->read.buffer_addr = cpu_to_le64(tx_buf[count].dma);
		tx_desc->read.olinfo_status = cpu_to_le32((tx_buf[count].len) << E1000_ADVTXD_PAYLEN_SHIFT);
		tx_desc->read.cmd_type_len = cpu_to_le32((cmd_type | (tx_buf[count].len)));
		send_index = send_index_next;
		count++;
		if (count >= send_cnt) {
			break;
		}
	}

	if (count > 0) {
		wmb();
		if (0 == send_index) {
			writel((u32) (tx_ring->count - 1), tx_ring->tail);
		} else {
			writel((u32) (send_index - 1), tx_ring->tail);
		}
		tx_ring->next_to_use = send_index;
	}
	schedule();
	return;
}

static void thruput_igb_send_clean(struct thruput_ctrl *ctrl)
{
	int count = 0;
	u16 clean_index;
	struct igb_ring *tx_ring;
	struct net_device *netdev;
	struct igb_adapter *adapter;
	union e1000_adv_tx_desc *tx_desc;

	netdev = ctrl->netdev;
	adapter = netdev_priv(netdev);
	tx_ring = adapter->tx_ring[0];
	clean_index = tx_ring->next_to_clean;
	while (true) {
		tx_desc = IGB_TX_DESC(tx_ring, clean_index);
		if (!(tx_desc->wb.status & cpu_to_le32(E1000_TXD_STAT_DD))) {
			tx_ring->next_to_clean = clean_index;
			schedule();
			break;
		}
		count++;
		tx_desc->wb.status = 0;
		tx_desc->wb.rsvd = 0;
		tx_desc->wb.nxtseq_seed = 0;
		clean_index += 1;
		if (clean_index >= tx_ring->count) {
			clean_index = 0;
		}
	}
	thruput_send_log(count, tx_ring->queue_index, ctrl->statistic);
	return;
}

static int thruput_igb_recv(struct thruput_ctrl *ctrl,
			    struct thruput_buf_info *buf_info)
{
	int count = 0;
	u16 clean_index;
	struct igb_ring *rx_ring;
	struct net_device *netdev;
	struct igb_adapter *adapter;
	union e1000_adv_rx_desc *rx_desc;
	struct thruput_desc_buf *tx_buf;
	struct thruput_desc_buf *rx_buf_curr;
	struct thruput_desc_buf *rx_buf_fill_hw;

	netdev = ctrl->netdev;
	adapter = netdev_priv(netdev);
	rx_ring = adapter->rx_ring[0];
	clean_index = rx_ring->next_to_clean;
	tx_buf = buf_info->tx_buf;
	rx_buf_curr = buf_info->rx_buf_curr;
	rx_buf_fill_hw = buf_info->rx_buf_fill_hw;

	while (true) {
		rx_desc = IGB_RX_DESC(rx_ring, clean_index);
		if (!rx_desc->wb.upper.status_error) {
			schedule();
			break;
		}

		tx_buf[count].len = le16_to_cpu(rx_desc->wb.upper.length);
		tx_buf[count].dma = rx_buf_curr[clean_index].dma;
		rx_desc->read.hdr_addr = 0;
		rx_desc->read.pkt_addr = rx_buf_fill_hw[clean_index].dma;
        
		count++;
		clean_index++;
		if (clean_index >= rx_ring->count) {
			clean_index = 0;
			buf_info->rx_buf_curr = rx_buf_fill_hw;
			buf_info->rx_buf_fill_hw = rx_buf_curr;
			break;
		}
		if (count > 24) {
			break;
		}
	}

	if (count > 0) {
		wmb();
		if (0 == clean_index) {
			writel((u32) (rx_ring->count - 1), rx_ring->tail);
		} else {
			writel((u32) (clean_index - 1), rx_ring->tail);
		}
		rx_ring->next_to_clean = clean_index;
	}
	thruput_recv_log(count, rx_ring->queue_index, ctrl->statistic);
	return count;
}

static void thruput_igb_rx_que_reinit
    (struct thruput_ctrl *ctrl, struct thruput_buf_info *buf_info) {
	int i;
	struct igb_ring *rx_ring;
	struct net_device *netdev;
	struct igb_adapter *adapter;
	union e1000_adv_rx_desc *rx_desc;

	netdev = ctrl->netdev;
	adapter = netdev_priv(netdev);
	rx_ring = adapter->rx_ring[0];

	for (i = 0; i < rx_ring->count; i++) {
		rx_desc = IGB_RX_DESC(rx_ring, i);
		rx_desc->read.hdr_addr = 0;
		rx_desc->read.pkt_addr = buf_info->rx_buf_curr[i].dma;
	}
	return;
}

static int thruput_igb_dma_map(struct thruput_ctrl *ctrl)
{
	int i, j;
	struct igb_ring *rx_ring;
	struct net_device *netdev;
	struct igb_adapter *adapter;
	struct thruput_buf_info *buf_info;

	netdev = ctrl->netdev;
	adapter = netdev_priv(netdev);
	rx_ring = adapter->rx_ring[0];

	for (i = 0; i < ctrl->task_num; i++) {
		buf_info = &(ctrl->buf_info[i]);
		for (j = 0; j < THRUPUT_BD_NUM; j++) {
			buf_info->rx_buf1[j].dma =
			    dma_map_single(rx_ring->dev, buf_info->dma_buf1[j].buf, 2048,
					   DMA_TO_DEVICE);
			if (dma_mapping_error(rx_ring->dev, buf_info->rx_buf1[j].dma)) {
				THRUPUT_PRT("dma_mapping_error\n");
				return (-1);
			}
			buf_info->rx_buf2[j].dma =
			    dma_map_single(rx_ring->dev, buf_info->dma_buf2[j].buf, 2048,
					   DMA_TO_DEVICE);
			if (dma_mapping_error(rx_ring->dev, buf_info->rx_buf2[j].dma)) {
				THRUPUT_PRT("dma_mapping_error\n");
				return (-1);
			}
		}
		buf_info->rx_buf_curr = buf_info->rx_buf1;
		buf_info->rx_buf_fill_hw = buf_info->rx_buf2;
	}
	return 0;
}

int thruput_igb_rx_que_num_get(struct net_device *netdev)
{
	int que_num;
	struct igb_adapter *adapter;

	adapter = netdev_priv(netdev);
	que_num = adapter->num_rx_queues;
	return que_num;
}

int thruput_igb_tx_que_num_get(struct net_device *netdev)
{
	int que_num;
	struct igb_adapter *adapter;

	adapter = netdev_priv(netdev);
	que_num = adapter->num_tx_queues;
	return que_num;
}

void thruput_igb_ops_init(struct thruput_ops *ops)
{
	ops->dev_send = thruput_igb_send;
	ops->dev_send_clean = thruput_igb_send_clean;
	ops->dev_recv = thruput_igb_recv;
	ops->dev_rx_que_init = thruput_igb_rx_que_reinit;
	ops->dev_irq_disable = thruput_igb_irq_disable;
	ops->dev_napi_stop = thruput_igb_napi_stop;
	ops->dev_dma_map = thruput_igb_dma_map;
	return;
}

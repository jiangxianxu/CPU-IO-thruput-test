

/*
 * 吞吐测试工具的STMMAC驱动ARCH层
 * 作者:姜先绪<jiangxianxv@163.com>
 *
 * version:v0.1
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>

#include "../stmmac/stmmac.h"

#include "thruput_test_core.h"


static inline void stmmac_disable_dma_irq(struct stmmac_priv *priv)
{
	priv->hw->dma->disable_dma_irq(priv->ioaddr);
}

static void thruput_stmmac_irq_disable(struct thruput_ctrl *ctrl)
{
	stmmac_disable_dma_irq(netdev_priv(ctrl->netdev));
	return;
}

static void thruput_stmmac_napi_stop(struct thruput_ctrl *ctrl)
{
	struct stmmac_priv *priv;

	priv = netdev_priv(ctrl->netdev);
	netif_stop_queue(ctrl->netdev);
	napi_disable(&priv->napi);
	del_timer_sync(&priv->txtimer);

	printk("%s %d\n", __FUNCTION__, __LINE__);
	return;
}

static void thruput_stmmac_send
    (struct thruput_ctrl *ctrl,
     struct thruput_buf_info *buf_info, int send_cnt) {
	int count = 0;
	int tx_size;
	u16 send_index;
	u16 send_index_next;
	struct net_device *netdev;
	struct stmmac_priv *priv;
	struct dma_desc *desc;
	struct thruput_desc_buf *tx_buf;

	netdev = ctrl->netdev;
	priv = netdev_priv(netdev);
	send_index = priv->cur_tx;
	tx_size = priv->dma_tx_size;
	tx_buf = buf_info->tx_buf;
	while (true) {
		send_index_next = send_index + 1;
		if (send_index_next >= tx_size) {
			send_index_next = 0;
		}

		if (send_index_next == priv->dirty_tx) {
			break;
		}

		if (priv->extend_desc || priv->extend_desc64)
			desc = (struct dma_desc *)(priv->dma_etx + send_index);
		else
			desc = priv->dma_tx + send_index;

		desc->des2 = tx_buf[count].dma;
		if (priv->extend_desc64) {
			desc->des3 = (tx_buf[count].dma) >> 32;
		}
		priv->hw->desc->prepare_tx_desc(desc, 1, tx_buf[count].len, 0, priv->mode);
		priv->hw->desc->close_tx_desc(desc);
		priv->hw->desc->set_tx_owner(desc);
		priv->hw->dma->enable_dma_transmission(priv->ioaddr);
		send_index = send_index_next;
		count++;
		if (count >= send_cnt) {
			break;
		}
	}

	priv->cur_tx = send_index;
	schedule();
	return;
}

static void thruput_stmmac_send_clean(struct thruput_ctrl *ctrl)
{
	int count = 0;
	int tx_size;
	u16 clean_index;
	struct net_device *netdev;
	struct stmmac_priv *priv;
	struct dma_desc *p;

	netdev = ctrl->netdev;
	priv = netdev_priv(netdev);
	clean_index = priv->dirty_tx;
	tx_size = priv->dma_tx_size;
	//printk("dirty_tx:%d cur_tx:%d\n", priv->dirty_tx, priv->cur_tx);
	while (true) {

		if (priv->extend_desc || priv->extend_desc64)
			p = (struct dma_desc *)(priv->dma_etx + clean_index);
		else
			p = priv->dma_tx + clean_index;

		/* Check if the descriptor is owned by the DMA. */
		if ((priv->hw->desc->get_tx_owner(p)) || (clean_index == priv->cur_tx))
		{
			//printk("dma_owner:%d clean_index:%d cur_tx:%d\n", priv->hw->desc->get_tx_owner(p), clean_index, priv->cur_tx);
			break;
		}

		priv->hw->ring->clean_desc3(priv, p);
		priv->hw->desc->release_tx_desc(p, priv->mode);

		count++;
		clean_index++;
		if (clean_index >= tx_size) {
			clean_index = 0;
		}
	}
	priv->dirty_tx = clean_index;
	thruput_send_log(count, 0, ctrl->statistic);
	return;
}

static int thruput_stmmac_recv(struct thruput_ctrl *ctrl,
			    struct thruput_buf_info *buf_info)
{
	int count = 0;
	int rx_size;
	int status;
	u16 clean_index;
	struct net_device *netdev;
	struct stmmac_priv *priv;
	struct dma_desc *p;
	struct thruput_desc_buf *tx_buf;
	struct thruput_desc_buf *rx_buf_curr;
	struct thruput_desc_buf *rx_buf_fill_hw;


	netdev = ctrl->netdev;
	priv = netdev_priv(netdev);
	clean_index = priv->cur_rx;
	rx_size = priv->dma_rx_size;
	tx_buf = buf_info->tx_buf;
	rx_buf_curr = buf_info->rx_buf_curr;
	rx_buf_fill_hw = buf_info->rx_buf_fill_hw;
	
	while (true) {
		if (priv->extend_desc || priv->extend_desc64)
			p = (struct dma_desc *)(priv->dma_erx + clean_index);
		else
			p = priv->dma_rx + clean_index;

		if (priv->hw->desc->get_rx_owner(p))
			break;

		/* read the status of the incoming frame */
		status = priv->hw->desc->rx_status(&priv->dev->stats, &priv->xstats, p);
#if 0
		if ((priv->extend_desc) && (priv->hw->desc->rx_extended_status)) {
			priv->hw->desc->rx_extended_status(&priv->dev->stats, &priv->xstats, priv->dma_erx + clean_index);
		}
#endif        
		tx_buf[count].len = priv->hw->desc->get_rx_frame_len(p, priv->plat->rx_coe);
		tx_buf[count].dma = rx_buf_curr[clean_index].dma;

		p->des2 = rx_buf_fill_hw[clean_index].dma;
		if (priv->extend_desc64) {
			p->des3 = rx_buf_fill_hw[clean_index].dma >> 32;
		}
		priv->hw->ring->refill_desc3(priv, p);
		priv->hw->desc->set_rx_owner(p);

		count++;
		clean_index++;
		if (clean_index >= rx_size) {
			clean_index = 0;
			buf_info->rx_buf_curr = rx_buf_fill_hw;
			buf_info->rx_buf_fill_hw = rx_buf_curr;
			break;
		}

		if (count > 32) {
			break;
		}
	}

	priv->cur_rx = clean_index;
	
	thruput_recv_log(count, 0, ctrl->statistic);
	return count;
}

static void thruput_stmmac_rx_que_reinit
    (struct thruput_ctrl *ctrl, struct thruput_buf_info *buf_info) 
{
	int i;
	struct net_device *netdev;
	struct stmmac_priv *priv;
	struct dma_desc *p;

	netdev = ctrl->netdev;
	priv = netdev_priv(netdev);

	for (i = 0; i < priv->dma_rx_size; i++) {

		if (priv->extend_desc|| priv->extend_desc64)
			p = (struct dma_desc *)(priv->dma_erx + i);
		else
			p = priv->dma_rx + i;

		p->des2 = buf_info->rx_buf_curr[i].dma;
		if (priv->extend_desc64) {
			p->des3 = buf_info->rx_buf_curr[i].dma >> 32;
		}

		priv->hw->ring->refill_desc3(priv, p);
		priv->hw->desc->set_rx_owner(p);
	}
	priv->cur_rx = priv->cur_rx % priv->dma_rx_size;
	priv->cur_tx = priv->cur_tx % priv->dma_tx_size;
	priv->dirty_tx = priv->dirty_tx % priv->dma_tx_size;
	return;
}

static int thruput_stmmac_dma_map(struct thruput_ctrl *ctrl)
{
	int i, j;
	struct net_device *netdev;
	struct stmmac_priv *priv;
	struct thruput_buf_info *buf_info;
	
	netdev = ctrl->netdev;
	priv = netdev_priv(netdev);
	
	for (i = 0; i < ctrl->task_num; i++) {
		buf_info = &(ctrl->buf_info[i]);
		for (j = 0; j < THRUPUT_BD_NUM; j++) {
			buf_info->rx_buf1[j].dma =
			    dma_map_single(priv->device, buf_info->dma_buf1[j].buf, 2048,
					   DMA_TO_DEVICE);
			if (dma_mapping_error(priv->device, buf_info->rx_buf1[j].dma)) {
				THRUPUT_PRT("dma_mapping_error\n");
				return (-1);
			}
			buf_info->rx_buf2[j].dma =
			    dma_map_single(priv->device, buf_info->dma_buf2[j].buf, 2048,
					   DMA_TO_DEVICE);
			if (dma_mapping_error(priv->device, buf_info->rx_buf2[j].dma)) {
				THRUPUT_PRT("dma_mapping_error\n");
				return (-1);
			}
		}
		buf_info->rx_buf_curr = buf_info->rx_buf1;
		buf_info->rx_buf_fill_hw = buf_info->rx_buf2;
	}
		
	return 0;
}

int thruput_stmmac_rx_que_num_get(struct net_device *netdev)
{
	return 1;
}

int thruput_stmmac_tx_que_num_get(struct net_device *netdev)
{
	return 1;
}

void thruput_stmmac_ops_init(struct thruput_ops *ops)
{
	ops->dev_send = thruput_stmmac_send;
	ops->dev_send_clean = thruput_stmmac_send_clean;
	ops->dev_recv = thruput_stmmac_recv;
	ops->dev_rx_que_init = thruput_stmmac_rx_que_reinit;
	ops->dev_irq_disable = thruput_stmmac_irq_disable;
	ops->dev_napi_stop = thruput_stmmac_napi_stop;
	ops->dev_dma_map = thruput_stmmac_dma_map;
	return;
}



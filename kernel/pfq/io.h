/***************************************************************
 *
 * (C) 2011-16 Nicola Bonelli <nicola@pfq.io>
 *             Loris Gazzarrini <loris.gazzarrini@iet.unipi.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 ****************************************************************/

#ifndef PF_Q_IO_H
#define PF_Q_IO_H

#include <engine/lang/module.h>
#include <engine/lang/engine.h>
#include <engine/lang/symtable.h>

#include <engine/percpu.h>
#include <engine/sock.h>
#include <engine/GC.h>

#include <pfq/netdev.h>


struct napi_struct;


int
pfq_receive(struct napi_struct *napi, struct sk_buff * skb, int direct);


typedef union
{
	uint64_t value;
	struct {
		uint32_t ok;
		uint32_t fail;
	};

} tx_res_t;



extern size_t pfq_sk_queue_recv(struct pfq_sock_opt *opt,
		                struct pfq_qbuff_refs *buffs,
		                unsigned long long skbs_mask,
		                int burst_len,
		                pfq_gid_t gid);

struct pfq_mbuff_xmit_context
{
	struct pfq_skb_pool	       *skb_pool;
	struct net		       *net;
	ktime_t			        now;
	unsigned long			jiffies;
	int				node;
};


/* socket queues */

extern tx_res_t pfq_sk_queue_xmit(struct pfq_sock *so, int qindex, int cpu, int node, atomic_t const *stop);


/* qbuff queues */

extern int pfq_xmit(struct qbuff *buff, struct net_device *dev, int queue, int more);
extern tx_res_t pfq_skb_queue_xmit(struct pfq_qbuff_queue *buffs, unsigned long long buffs_mask, struct net_device *dev, int queue_index);


extern int pfq_lazy_xmit(struct qbuff * buff, struct net_device *dev, int queue_index);
extern int pfq_qbuff_queue_lazy_xmit_run(struct pfq_qbuff_queue *queue, struct pfq_endpoint_info const *info);


#define pfq_qbuff_queue_lazy_xmit(buffs, mask, dev, queue_index) ({ \
		int enforce = STATIC_TYPE(unsigned long long, mask) && \
			      STATIC_TYPE(struct net_device *, dev) && \
			      STATIC_TYPE(int, queue_index); \
		struct qbuff * buff; \
		int i, n = 0; \
		(void)enforce; \
                \
		for_each_qbuff_with_mask(mask, buffs, buff, i) \
		{   \
			if (pfq_lazy_xmit(buff, dev, queue_index)) \
				++n; \
		} \
		n; \
	})

#endif /* PF_Q_IO_H */

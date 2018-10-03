﻿// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2015 Oslo and Akershus University College of Applied Sciences
// and Alfred Bratterud
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// loosely based on iPXE driver as well as from Linux driver by VMware
#include "vmxnet3.hpp"
#include "vmxnet3_queues.hpp"

#include <kernel/events.hpp>
#include <smp>
#include <statman>
#include <info>
#include <cassert>
#include <malloc.h>
static std::vector<vmxnet3*> deferred_devs;

#define VMXNET3_REV1_MAGIC 0xbabefee1
#define VMXNET3_MAX_BUFFER_LEN 0x4000
#define VMXNET3_DMA_ALIGN  512
#define VMXNET3_IT_AUTO    0
#define VMXNET3_IMM_AUTO   0
#define VMXNET3_IMM_ACTIVE 1

#define VMXNET3_NUM_TX_COMP  vmxnet3::NUM_TX_DESC
#define VMXNET3_NUM_RX_COMP  (vmxnet3::NUM_RX_DESC1 + vmxnet3::NUM_RX_DESC2)
static const int VMXNET3_TX_FILL = vmxnet3::NUM_TX_DESC-1;
static const int VMXNET3_RX_FILL = (vmxnet3::NUM_RX_DESC1+vmxnet3::NUM_RX_DESC2);

/**
 * DMA areas
 *
 * These are arranged in order of decreasing alignment, to allow for a
 * single allocation
 */
struct vmxnet3_dma {
  /** TX ring */
  struct vmxnet3_tx_desc tx_desc[vmxnet3::NUM_TX_DESC];
  struct vmxnet3_tx_comp tx_comp[VMXNET3_NUM_TX_COMP];
  /** RX ring */
  struct vmxnet3_rx {
    struct vmxnet3_rx_desc desc1[vmxnet3::NUM_RX_DESC1];
    struct vmxnet3_rx_desc desc2[vmxnet3::NUM_RX_DESC2];
    struct vmxnet3_rx_comp comp[VMXNET3_NUM_RX_COMP];
  };
  struct vmxnet3_rx rx[vmxnet3::NUM_RX_QUEUES];
  /** Queue descriptors */
  struct vmxnet3_queues queues;
  /** Shared area */
  struct vmxnet3_shared shared;

} __attribute__ ((aligned(VMXNET3_DMA_ALIGN)));

#define PRODUCT_ID    0x7b0
#define REVISION_ID   0x1

#define PCI_BAR_PT     0
#define PCI_BAR_VD     1
#define PCI_BAR_MSIX   2

#define VMXNET3_VD_CMD    0x20
#define VMXNET3_VD_MAC_LO 0x28
#define VMXNET3_VD_MAC_HI 0x30
#define VMXNET3_VD_ECR    0x40

/** Commands */
enum vmxnet3_command {
  VMXNET3_CMD_FIRST_SET = 0xcafe0000,
  VMXNET3_CMD_ACTIVATE_DEV = VMXNET3_CMD_FIRST_SET,
  VMXNET3_CMD_QUIESCE_DEV,
  VMXNET3_CMD_RESET_DEV,
  VMXNET3_CMD_UPDATE_RX_MODE,
  VMXNET3_CMD_UPDATE_MAC_FILTERS,
  VMXNET3_CMD_UPDATE_VLAN_FILTERS,
  VMXNET3_CMD_UPDATE_RSSIDT,
  VMXNET3_CMD_UPDATE_IML,
  VMXNET3_CMD_UPDATE_PMCFG,
  VMXNET3_CMD_UPDATE_FEATURE,
  VMXNET3_CMD_LOAD_PLUGIN,

  VMXNET3_CMD_FIRST_GET = 0xf00d0000,
  VMXNET3_CMD_GET_QUEUE_STATUS = VMXNET3_CMD_FIRST_GET,
  VMXNET3_CMD_GET_STATS,
  VMXNET3_CMD_GET_LINK,
  VMXNET3_CMD_GET_PERM_MAC_LO,
  VMXNET3_CMD_GET_PERM_MAC_HI,
  VMXNET3_CMD_GET_DID_LO,
  VMXNET3_CMD_GET_DID_HI,
  VMXNET3_CMD_GET_DEV_EXTRA_INFO,
  VMXNET3_CMD_GET_CONF_INTR
};

inline uint32_t mmio_read32(uintptr_t location)
{
  return *(uint32_t volatile*) location;
}
inline void mmio_write32(uintptr_t location, uint32_t value)
{
  *(uint32_t volatile*) location = value;
}

static inline uint16_t buffer_size_for_mtu(const uint16_t mtu)
{
  const uint16_t header = sizeof(net::Packet) + vmxnet3::DRIVER_OFFSET;
  uint16_t total = header + sizeof(net::ethernet::VLAN_header) + mtu;
  if (total & 15) total += 16 - (total & 15);
  //if (total & 2047) total += 2048 - (total & 2047);
  assert(total <= 16384 && "Buffers larger than 16k are not supported");
  return total;
}

vmxnet3::vmxnet3(hw::PCI_Device& d, const uint16_t mtu) :
    Link(Link_protocol{{this, &vmxnet3::transmit}, mac()}),
    m_pcidev(d), m_mtu(mtu),
    stat_sendq_cur{Statman::get().create(Stat::UINT32, device_name() + ".sendq_now").get_uint32()},
    stat_sendq_max{Statman::get().create(Stat::UINT32, device_name() + ".sendq_max").get_uint32()},
    stat_rx_refill_dropped{Statman::get().create(Stat::UINT64, device_name() + ".rx_refill_dropped").get_uint64()},
    stat_sendq_dropped{Statman::get().create(Stat::UINT64, device_name() + ".sendq_dropped").get_uint64()},
    bufstore_{1024, buffer_size_for_mtu(mtu)}
{
  INFO("vmxnet3", "Driver initializing (rev=%#x)", d.rev_id());
  assert(d.rev_id() == REVISION_ID);
  Statman::get().create(Stat::UINT32, device_name() + ".buffer_size")
      .get_uint32() = bufstore_.bufsize();

  // find and store capabilities
  d.parse_capabilities();
  // find BARs etc.
  d.probe_resources();

  if (d.msix_cap())
  {
    d.init_msix();
    uint8_t msix_vectors = d.get_msix_vectors();
    INFO2("[x] Device has %u MSI-X vectors", msix_vectors);
    assert(msix_vectors >= 3);
    if (msix_vectors > 2 + NUM_RX_QUEUES) msix_vectors = 2 + NUM_RX_QUEUES;

    for (int i = 0; i < msix_vectors; i++)
    {
      auto irq = Events::get().subscribe(nullptr);
      this->irqs.push_back(irq);
      d.setup_msix_vector(SMP::cpu_id(), IRQ_BASE + irq);
    }

    Events::get().subscribe(irqs[0], {this, &vmxnet3::msix_evt_handler});
    Events::get().subscribe(irqs[1], {this, &vmxnet3::msix_xmit_handler});
    for (int q = 0; q < NUM_RX_QUEUES; q++)
    Events::get().subscribe(irqs[2 + q], {this, &vmxnet3::msix_recv_handler});
  }
  else {
    assert(0 && "This driver does not support legacy IRQs");
  }

  // dma areas
  this->iobase = d.get_bar(PCI_BAR_VD).start;
  assert(this->iobase);
  this->ptbase = d.get_bar(PCI_BAR_PT).start;
  assert(this->ptbase);

  // verify and select version
  bool ok = check_version();
  assert(ok);

  // reset device
  ok = reset();
  assert(ok);

  // get mac address
  retrieve_hwaddr();

  // check link status
  auto link_spd = check_link();
  if (link_spd) {
    INFO2("Link up at %u Mbps", link_spd);
  }
  else {
    INFO2("LINK DOWN! :(");
    return;
  }

  // set MAC
  set_hwaddr(this->hw_addr);

  // initialize DMA areas
  this->dma = (vmxnet3_dma*) memalign(VMXNET3_DMA_ALIGN, sizeof(vmxnet3_dma));
  memset(this->dma, 0, sizeof(vmxnet3_dma));

  auto& queues = dma->queues;
  // setup tx queues
  queues.tx.cfg.desc_address = (uintptr_t) &dma->tx_desc;
  queues.tx.cfg.comp_address = (uintptr_t) &dma->tx_comp;
  queues.tx.cfg.num_desc     = vmxnet3::NUM_TX_DESC;
  queues.tx.cfg.num_comp     = VMXNET3_NUM_TX_COMP;
  queues.tx.cfg.intr_index   = 1;
  // temp rxq buffer storage
  memset(tx.buffers, 0, sizeof(tx.buffers));

  // setup rx queues
  for (int q = 0; q < NUM_RX_QUEUES; q++)
  {
  //  memset(rx[q].buffers, 0, sizeof(rx[q].buffers));
    rx[q].desc0 = &dma->rx[q].desc1[0];
    rx[q].desc1 = &dma->rx[q].desc2[0];
    rx[q].comp  = &dma->rx[q].comp[0];
    rx[q].index = q;

    auto& queue = queues.rx[q];
    queue.cfg.desc_address[0] = (uintptr_t) rx[q].desc0;
    queue.cfg.desc_address[1] = (uintptr_t) rx[q].desc1;
    queue.cfg.comp_address    = (uintptr_t) rx[q].comp;
    queue.cfg.num_desc[0]  = vmxnet3::NUM_RX_DESC1;
    queue.cfg.num_desc[1]  = vmxnet3::NUM_RX_DESC2;
    queue.cfg.num_comp     = vmxnet3::NUM_RX_DESC1 + vmxnet3::NUM_RX_DESC2;
    queue.cfg.driver_data_len = sizeof(vmxnet3_rx_desc)*(vmxnet3::NUM_RX_DESC1 + vmxnet3::NUM_RX_DESC2);
    queue.cfg.intr_index = 2 + q;
  }

  auto& shared = dma->shared;
  // setup shared physical area
  shared.magic = VMXNET3_REV1_MAGIC;
  shared.misc.guest_info.arch =
      (sizeof(void*) == 4) ? GOS_BITS_32_BITS : GOS_BITS_64_BITS;
  shared.misc.guest_info.type = GOS_TYPE_LINUX;
  shared.misc.version         = VMXNET3_VERSION_MAGIC;
  shared.misc.version_support     = 1;
  shared.misc.upt_version_support = 1;
  shared.misc.upt_features        = 0;
  shared.misc.driver_data_address = (uintptr_t) &dma;
  shared.misc.queue_desc_address  = (uintptr_t) &dma->queues;
  shared.misc.driver_data_len     = sizeof(vmxnet3_dma);
  shared.misc.queue_desc_len      = sizeof(vmxnet3_queues);
  shared.misc.mtu = max_packet_len(); // 60-9000
  shared.misc.num_tx_queues  = 1;
  shared.misc.num_rx_queues  = NUM_RX_QUEUES;
  shared.interrupt.mask_mode = VMXNET3_IT_AUTO | (VMXNET3_IMM_AUTO << 2);
  shared.interrupt.num_intrs = 2 + NUM_RX_QUEUES;
  shared.interrupt.event_intr_index = 0;
  memset(shared.interrupt.moderation_level, UPT1_IML_ADAPTIVE, VMXNET3_MAX_INTRS);
  shared.interrupt.control   = 0x1; // disable all
  shared.rx_filter.mode =
      VMXNET3_RXM_UCAST | VMXNET3_RXM_BCAST | VMXNET3_RXM_ALL_MULTI;

  // location of shared area to device
  uintptr_t shabus = (uintptr_t) &shared;
  mmio_write32(this->iobase + 0x10, shabus); // shared low
  mmio_write32(this->iobase + 0x18, 0x0);    // shared high

  // activate device
  int status = command(VMXNET3_CMD_ACTIVATE_DEV);
  if (status) {
    assert(0 && "Failed to activate device");
  }

  // initialize and fill RX queue...
  for (int q = 0; q < NUM_RX_QUEUES; q++)
  {
    memset(&rx[q].desc0[0],0,sizeof(vmxnet3_rx_desc)*rx[q].desc0_size);
    memset(&rx[q].desc1[0],0,sizeof(vmxnet3_rx_desc)*rx[q].desc1_size);
    rx[q].id0=q;
    rx[q].id1=q+NUM_RX_QUEUES;
    refill(rx[q]);
  }

  // deferred transmit
  this->deferred_irq = Events::get().subscribe(handle_deferred);

  // enable interrupts
  enable_intr(0);
  enable_intr(1);
  for (int q = 0; q < NUM_RX_QUEUES; q++)
      enable_intr(2 + q);
}

uint32_t vmxnet3::command(uint32_t cmd)
{
  mmio_write32(this->iobase + VMXNET3_VD_CMD, cmd);
  return mmio_read32(this->iobase + VMXNET3_VD_CMD);
}

bool vmxnet3::check_version()
{
  uint32_t maj_ver = mmio_read32(this->iobase + 0x0);
  uint32_t min_ver = mmio_read32(this->iobase + 0x8);
  INFO("vmxnet3", "Version %d.%d", maj_ver, min_ver);

  // select version we support
  mmio_write32(this->iobase + 0x0, 0x1);
  mmio_write32(this->iobase + 0x8, 0x1);
  return true;
}
uint16_t vmxnet3::check_link()
{
  auto state = command(VMXNET3_CMD_GET_LINK);
  this->link_state_up = (state & 1) != 0;
  if (this->link_state_up)
      return state >> 16;
  else
      return 0;
}
bool vmxnet3::reset()
{
  auto res = command(VMXNET3_CMD_RESET_DEV);
  return res == 0;
}

void vmxnet3::retrieve_hwaddr()
{
  struct {
    uint32_t lo;
    uint32_t hi;
  } mac;
  mac.lo = mmio_read32(this->iobase + VMXNET3_VD_MAC_LO);
  mac.hi = mmio_read32(this->iobase + VMXNET3_VD_MAC_HI);
  // avoid memcpy() when we can just use bitwise-operators
  this->hw_addr.minor = mac.lo & 0xFFFF;
  this->hw_addr.major = (mac.lo >> 16) | (mac.hi << 16);
  INFO2("MAC address: %s", hw_addr.to_string().c_str());
}
void vmxnet3::set_hwaddr(MAC::Addr& addr)
{
  struct {
    uint32_t lo;
    uint32_t hi;
  } mac {0, 0};
  // ETH_ALEN = 6
  memcpy(&mac, &addr, sizeof(addr));

  mmio_write32(this->iobase + VMXNET3_VD_MAC_LO, mac.lo);
  mmio_write32(this->iobase + VMXNET3_VD_MAC_HI, mac.hi);
}
void vmxnet3::enable_intr(uint8_t idx) noexcept
{
#define VMXNET3_PT_IMR 0x0
  mmio_write32(this->ptbase + VMXNET3_PT_IMR + idx * 8, 0);
}
void vmxnet3::disable_intr(uint8_t idx) noexcept
{
  mmio_write32(this->ptbase + VMXNET3_PT_IMR + idx * 8, 1);
}

#define VMXNET3_PT_TXPROD  0x600
#define VMXNET3_PT_RXPROD1 0x800
#define VMXNET3_PT_RXPROD2 0xa00

#define VMXNET3_RXF_GEN  0x80000000UL
#define VMXNET3_RXCF_GEN 0x80000000UL
#define VMXNET3_TXF_GEN  0x00004000UL


void vmxnet3::refill(rxring_state& rxq)
{
   // printf("Refill idx1=%d idx2=%d\n",rxq.ring0_producers,rxq.ring1_producers);
    //this is the lazy way of doing it .. vector of released descriptors would be more efficient
    auto prod0=rxq.ring0_producers;
    auto prod1=rxq.ring1_producers;

    bool flipped1=false;
    bool flipped2=false;
    while (rxq.ring0_unallocated)
    {
        // break when not allowed to refill anymore
        if (rxq.prod_count > 0
         && not Nic::buffers_still_available(bufstore().buffers_in_use()))
        {
          printf("Out of buffers\n");
          stat_rx_refill_dropped += VMXNET3_RX_FILL - rxq.prod_count;
          break;
        }
        auto i = rxq.ring0_producers;
        if (rxq.desc0[i].address == 0)
        {
            //TODO check that we actually get a packet..
            auto* pkt_data = bufstore().get_buffer();
            rxq.desc0[i].address=(uintptr_t) &pkt_data[sizeof(net::Packet) + DRIVER_OFFSET];
            rxq.desc0[i].flags=(max_packet_len()&0x7FFF);
            if (rxq.ring0_gen)
                rxq.desc0[i].flags |= VMXNET3_RXF_GEN;

            rxq.ring0_unallocated--;

            rxq.ring0_producers++;
            if (UNLIKELY(rxq.ring0_producers == rxq.desc0_size))
            {
        //        printf("flipping generation on ring0 \n");
                rxq.ring0_gen=rxq.ring0_gen^1;
                rxq.ring0_producers=0;
               // flipped1=true;
                break;
            }
            //rxq.producers++;
            //rxq.prod_count++;
        }
    }
    //for (auto i = 0 ; i < rxq.desc1_size;i++)
    while(rxq.ring1_unallocated)
    {
        // break when not allowed to refill anymore
        if (rxq.prod_count > 0
         && not Nic::buffers_still_available(bufstore().buffers_in_use()))
        {
          printf("Out of buffers\n");
          stat_rx_refill_dropped += VMXNET3_RX_FILL - rxq.prod_count;
          break;
        }
        auto i = rxq.ring1_producers;
         //TODO check that we actually get a packet..
        auto* pkt_data = bufstore().get_buffer();
        rxq.desc1[i].address=(uintptr_t) &pkt_data[sizeof(net::Packet) + DRIVER_OFFSET];
        rxq.desc1[i].flags=(max_packet_len()&0x7FFF);
        if (rxq.ring1_gen)
            rxq.desc1[i].flags |= VMXNET3_RXF_GEN;
        rxq.ring1_unallocated--;
        rxq.ring1_producers++;
        if (UNLIKELY(rxq.ring1_producers == rxq.desc1_size))
        {

         //   printf("flipping generation on ring1 \n");
            rxq.ring1_gen=rxq.ring1_gen^1;
            rxq.ring1_producers=0;
            break;
        }
    }

    if (prod0 != rxq.ring0_producers)
    {
        mmio_write32(this->ptbase + VMXNET3_PT_RXPROD1 + rxq.id0*8,
                     rxq.ring0_producers);
    }

    if (prod1 != rxq.ring1_producers)
    {
        mmio_write32(this->ptbase + VMXNET3_PT_RXPROD2 + rxq.id1*8,
                     rxq.ring1_producers);
    }
}

net::Packet_ptr
vmxnet3::recv_packet(uint8_t* data, uint16_t size)
{
  auto* ptr = (net::Packet*) (data - DRIVER_OFFSET - sizeof(net::Packet));
  new (ptr) net::Packet(
        DRIVER_OFFSET,
        size,
        DRIVER_OFFSET + size,
        &bufstore());
  return net::Packet_ptr(ptr);
}
net::Packet_ptr
vmxnet3::create_packet(int link_offset)
{
  auto* ptr = (net::Packet*) bufstore().get_buffer();
  new (ptr) net::Packet(
        DRIVER_OFFSET + link_offset,
        0,
        DRIVER_OFFSET + frame_offset_link() + MTU(),
        &bufstore());
  return net::Packet_ptr(ptr);
}

void vmxnet3::msix_evt_handler()
{
  uint32_t evts = dma->shared.ecr;
  if (evts == 0) return;
  // ack all events
  mmio_write32(this->iobase + VMXNET3_VD_ECR, evts);

  if (evts & 0x1)
  {
    printf("[vmxnet3] rxq error: %#x\n", evts);
  }
  if (evts & 0x2)
  {
    printf("[vmxnet3] txq error: %#x\n", evts);
  }
  if (evts & 0x4)
  {
    this->check_link();
    printf("[vmxnet3] resume from sleep? link up = %d\n",
          this->link_state_up);
  }
  if (evts & 0x8)
  {
    this->check_link();
  }
  // unknown event
  if (evts & ~0xF)
  {
    printf("[vmxnet3] unknown events: %#x\n", evts);
  }
}
void vmxnet3::msix_xmit_handler()
{
  this->disable_intr(1);
  this->transmit_handler();
  this->enable_intr(1);
}
void vmxnet3::msix_recv_handler()
{
  for (int q = 0; q < NUM_RX_QUEUES; q++)
  {
      this->receive_handler(q);
  }
}

bool vmxnet3::transmit_handler()
{
//  printf("Transmit\n");
  bool transmitted = false;
  while (true)
  {
    uint32_t idx = tx.consumers % VMXNET3_NUM_TX_COMP;
    uint32_t gen = (tx.consumers & VMXNET3_NUM_TX_COMP) ? 0 : VMXNET3_TXCF_GEN;

    auto& comp = dma->tx_comp[idx];
    if (gen != (comp.flags & VMXNET3_TXCF_GEN)) break;

    tx.consumers++;

    int desc = comp.index % vmxnet3::NUM_TX_DESC;
    if (tx.buffers[desc] == nullptr) {
      printf("empty buffer? comp=%d, desc=%d\n", idx, desc);
      continue;
    }
    auto* packet = (net::Packet*) (tx.buffers[desc] - DRIVER_OFFSET - sizeof(net::Packet));
    delete packet; // call deleter on Packet to release it
    tx.buffers[desc] = nullptr;
  }
  // try to send sendq first
  if (this->can_transmit() && !sendq.empty()) {
    this->transmit(nullptr);
    transmitted = true;
  }
  // if we can still send more, message network stack
  //printf("There are now %d tokens free\n", tx_tokens_free());
  if (this->can_transmit()) {
    auto tok = tx_tokens_free();
    transmit_queue_available_event(tok);
    if (tx_tokens_free() != tok) transmitted = true;
  }
  return transmitted;
}

bool vmxnet3::receive_handler(const int Q)
{
  std::vector<net::Packet_ptr> recvq;
  this->disable_intr(2 + Q);

  VmxNet3_RxComp *cmp=static_cast<VmxNet3_RxComp*>(&dma->rx[Q].comp[rx[Q].consumers % VMXNET3_NUM_RX_COMP]);
  while (rx[Q].comp_gen == cmp->gen())
  {
      //make sure we block any premature reading beyond gen
    __asm volatile("lfence" ::: "memory");
    //  __sw_barrier();
 //   printf("start receiving something\n");
  //  uint32_t comp_idx = rx[Q].consumers % VMXNET3_NUM_RX_COMP;
    //uint32_t gen = (rx[Q].consumers & VMXNET3_NUM_RX_COMP) ? 0 : VMXNET3_RXCF_GEN;
    //printf("comp idx %d\n",comp_idx);
   // auto& comp = dma->rx[Q].comp[comp_idx];

    //cmp=static_cast<VmxNet3_RxComp*>(&dma->rx[Q].comp[comp_idx]);

    //dangerous .. read ahead issues ?
  /*  if (cmp->length() == 0)
    {
        printf("zero length packet len=%08x,index=%08x,flags=%08x\n",comp.len,comp.index,comp.flags);
        break;
    }*/

    //printf("rx_idx %d rx_rq_id=%d\n",cmp->idx(),cmp->qid());
    //update CMP to next buffer

  //  BUG_ON(rcd->rqID != rq->qid && rcd->rqID != rq->qid2 &&
//rcd->rqID != rq->dataRingQid);

    VmxNet3RxDesc *desc;
    //vmxnet3_rx_desc *desc;
    //printf("cmp->qid(%d)",cmp->qid());
    if (cmp->qid() == rx[Q].id1)
    {
        desc = static_cast<VmxNet3RxDesc *>(&dma->rx[Q].desc2[rx[Q].ring1_consumers++]);
        if (UNLIKELY(rx[Q].ring1_consumers == rx[Q].desc1_size))
            rx[Q].ring1_consumers=0;
    }
    else if (cmp->qid() == rx[Q].id0)
    {
        desc = static_cast<VmxNet3RxDesc *>(&dma->rx[Q].desc1[rx[Q].ring0_consumers++]);
        if (UNLIKELY(rx[Q].ring0_consumers == rx[Q].desc0_size))
            rx[Q].ring0_consumers=0;
    }
    else
    {
        //TODO handle data queue?
        printf("Unknown queue ID \n");
        assert(0);
    }

    //int idx=cmp->idx();
  //  printf("Ring %d idx %d\n",cmp->qid(),cmp->idx());
  //  printf("Desc len %d desc gen %d\n",desc->len(),desc->gen());


    // mask out length TODO read from desc header?
    int len = cmp->length();// & (VMXNET3_MAX_BUFFER_LEN-1);
    // get buffer and construct packet
  //  uint32_t len = desc->flags
 //   printf("desc %d len %d\n",desc,len);
    assert(desc->address != 0);

   //TODO if len == 0 recieve and free packet!!
   recvq.push_back(recv_packet((unsigned char *)desc->address, len));
   //relq.push_back(*desc);

   desc->address=0;
   rx[Q].consumers++;

   if (UNLIKELY(rx[Q].consumers == rx[Q].comp_size))
   {
       rx[Q].comp_gen =rx[Q].comp_gen^1;
       rx[Q].consumers=0;
   }

   if (cmp->qid() == rx[Q].id1)
   {
       rx[Q].ring1_unallocated++;
       if (rx[Q].ring1_unallocated == rx[Q].desc1_size)
           break;
   }
   else
   {

       rx[Q].ring0_unallocated++;
       if (rx[Q].ring0_unallocated == rx[Q].desc0_size)
           break;
    }



    cmp=static_cast<VmxNet3_RxComp*>(&rx[Q].comp[rx[Q].consumers]);
  }

  // refill always
  if (!recvq.empty()) {
  //  printf("refill packets\n");
    this->refill(rx[Q]);
     __asm volatile("mfence" ::: "memory");
  }


  this->enable_intr(2 + Q);

  // handle packets
  for (auto& pckt : recvq) {
 //   printf("link receive\n");
    Link::receive(std::move(pckt));
  }
  return recvq.empty() == false;
}

void vmxnet3::transmit(net::Packet_ptr pckt_ptr)
{
//  printf("TRANSMIT PACKET\n");
  while (pckt_ptr != nullptr)
  {
    if (not Nic::sendq_still_available(this->sendq.size())) {
      stat_sendq_dropped += pckt_ptr->chain_length();
      break;
    }
    auto tail = pckt_ptr->detach_tail();
    sendq.emplace_back(std::move(pckt_ptr));
    pckt_ptr = std::move(tail);
  }
  // send as much as possible from sendq
  while (!sendq.empty() && can_transmit())
  {
    auto* packet = sendq.front().release();
    sendq.pop_front();
    // transmit released buffer
    transmit_data(packet->buf() + DRIVER_OFFSET, packet->size());
  }
  // update sendq stats
  stat_sendq_cur = sendq.size();
  stat_sendq_max = std::max(stat_sendq_max, stat_sendq_cur);

  // delay dma message until we have written as much as possible
  if (!deferred_kick)
  {
    deferred_kick = true;
    if (this->already_polling == false) {
        deferred_devs.push_back(this);
        Events::get().trigger_event(deferred_irq);
    }
  }
}
inline int  vmxnet3::tx_flush_diff() const noexcept
{
  return tx.producers - tx.flushvalue;
}
inline int  vmxnet3::tx_tokens_free() const noexcept
{
  return VMXNET3_TX_FILL - (tx.producers - tx.consumers);
}
inline bool vmxnet3::can_transmit() const noexcept
{
  return tx_tokens_free() > 0 && this->link_state_up;
}

void vmxnet3::transmit_data(uint8_t* data, uint16_t data_length)
{
#define VMXNET3_TXF_EOP 0x000001000UL
#define VMXNET3_TXF_CQ  0x000002000UL
  auto idx = tx.producers % vmxnet3::NUM_TX_DESC;
  auto gen = (tx.producers & vmxnet3::NUM_TX_DESC) ? 0 : VMXNET3_TXF_GEN;
  tx.producers++;

  assert(tx.buffers[idx] == nullptr);
  tx.buffers[idx] = data;

  auto& desc = dma->tx_desc[idx];
  desc.address  = (uintptr_t) tx.buffers[idx];
  desc.flags[0] = gen | data_length;
  desc.flags[1] = VMXNET3_TXF_CQ | VMXNET3_TXF_EOP;
}

void vmxnet3::flush()
{
  if (tx_flush_diff() > 0)
  {

    auto idx = tx.producers % vmxnet3::NUM_TX_DESC;
    mmio_write32(ptbase + VMXNET3_PT_TXPROD, idx);
    tx.flushvalue = tx.producers;
  }
}

void vmxnet3::handle_deferred()
{
  for (auto* dev : deferred_devs)
  {
    dev->flush();
    dev->deferred_kick = false;
  }
  deferred_devs.clear();
}

void vmxnet3::poll()
{
  if (tqa_events_.empty()) return;
  if (this->already_polling) return;
  this->already_polling = true;

  bool work;
  do {
    work = false;
    for (int q = 0; q < NUM_RX_QUEUES; q++)
        work |= receive_handler(q);
    // transmit
    work |= transmit_handler();
    // immediately flush when possible
    if (this->deferred_kick) {
        this->deferred_kick = false;
        this->flush();
    }
  } while (work);

  this->already_polling = false;
}

void vmxnet3::deactivate()
{
  // disable all queues
  this->disable_intr(0);
  this->disable_intr(1);
  for (int q = 0; q < NUM_RX_QUEUES; q++)
    this->disable_intr(2 + q);

  // reset this device
  this->reset();
}

void vmxnet3::move_to_this_cpu()
{
  bufstore().move_to_this_cpu();

  if (m_pcidev.has_msix())
  {
    for (size_t i = 0; i < irqs.size(); i++)
    {
      this->irqs[i] = Events::get().subscribe(nullptr);
      m_pcidev.rebalance_msix_vector(i, SMP::cpu_id(), IRQ_BASE + this->irqs[i]);
    }
  }
}

#include <kernel/pci_manager.hpp>
__attribute__((constructor))
static void register_func()
{
  PCI_manager::register_nic(PCI::VENDOR_VMWARE, PRODUCT_ID, &vmxnet3::new_instance);
}

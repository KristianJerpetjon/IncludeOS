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

#include <hw/pci_device.hpp>
#include <net/link_layer.hpp>
#include <net/ethernet/ethernet_8021q.hpp>
#include <deque>
#include <vector>


struct vmxnet3_dma;
struct vmxnet3_rx_desc;
struct vmxnet3_rx_comp;

class vmxnet3 : public net::Link_layer<net::Ethernet>
{
public:
  using Link          = net::Link_layer<net::Ethernet>;
  using Link_protocol = Link::Protocol;
  static const int DRIVER_OFFSET = 2;
  static const int NUM_RX_QUEUES = 1;
  static const int NUM_TX_DESC   = 128;
  static const int NUM_RX_DESC1   = 1024;
  static const int NUM_RX_DESC2   = 256;

  static std::unique_ptr<Nic> new_instance(hw::PCI_Device& d, const uint16_t MTU)
  { return std::make_unique<vmxnet3>(d, MTU); }

  /** Human readable name. */
  const char* driver_name() const override {
    return "vmxnet3";
  }

  /** Mac address. */
  const MAC::Addr& mac() const noexcept override {
    return this->hw_addr;
  }

  uint16_t MTU() const noexcept override {
    return m_mtu;
  }

  uint16_t max_packet_len() const noexcept {
    return sizeof(net::ethernet::VLAN_header) + MTU();
  }

  net::downstream create_physical_downstream() override
  { return {this, &vmxnet3::transmit}; }

  net::Packet_ptr create_packet(int) override;

  /** Linklayer input. Hooks into IP-stack bottom, w.DOWNSTREAM data.*/
  void transmit(net::Packet_ptr pckt);

  /** Constructor. @param pcidev an initialized PCI device. */
  vmxnet3(hw::PCI_Device& pcidev, uint16_t MTU);

  /** Space available in the transmit queue, in packets */
  size_t transmit_queue_available() override {
    return tx_tokens_free();
  }

  auto& bufstore() noexcept { return bufstore_; }

  void flush() override;

  void deactivate() override;

  void move_to_this_cpu() override;

  void poll() override;

private:
  void msix_evt_handler();
  void msix_xmit_handler();
  void msix_recv_handler();
  bool receive_handler(int);
  bool transmit_handler();
  void enable_intr(uint8_t idx) noexcept;
  void disable_intr(uint8_t idx) noexcept;

  inline int  tx_flush_diff() const noexcept;
  inline int  tx_tokens_free() const noexcept;
  inline bool can_transmit() const noexcept;
  void transmit_data(uint8_t* data, uint16_t);
  net::Packet_ptr recv_packet(uint8_t* data, uint16_t);

  // tx/rx ring state
  struct ring_stuff {
    uint8_t* buffers[NUM_TX_DESC];
    uint32_t producers  = 0;
    uint32_t prod_count = 0;
    uint32_t consumers  = 0;
    uint32_t flushvalue = 0;
  };

  template<class T>
  class RingData
  {
  public:
    //RingData(uint8_t id,uint32_t size,ptr,hwaddr)
    //TODO move to constructor!!
    void setSize(uint32_t _size)
    {
      size=_size;
      unallocated=_size;
    }
    //void setUnallocated(uint32_t unalloc) { unallocated = unalloc;}
    void setHWIndex(uint32_t _hwindex) {hwIndex=_hwindex; }
    void setRingId(uint8_t _id) { id = _id;}
    void setDesc(T *ptr) { desc=ptr;}

    inline void flip() noexcept
    {
      generation=generation^1;
    }
    uint32_t gen() const noexcept { return generation; }
    uint32_t head() const noexcept{ return producers;}
    inline uint32_t nextHead() noexcept {
      unallocated--; //remove this and we save some more cycles
      producers++;
      if (UNLIKELY(producers == size))
      {
         generation^=1;
         producers=0;
      }
      return head();
    }
    uint32_t tail() const { return consumers;}

    inline uint32_t nextTail() noexcept {
      consumers++;
      if (UNLIKELY(consumers == size))
          consumers=0;
      unallocated++;

      return tail();
    }
    uint32_t free() const { return unallocated; }
    bool empty() const { return unallocated == size; }

    T& atTail() { return desc[consumers];}
    T& atHead() { return desc[producers];}

    uint32_t hwIndex=0;
    uint32_t size=0;
    uint8_t id=255;
  private:
    uint32_t producers=0;
    uint32_t generation=1;
    uint32_t unallocated=0;
    uint32_t consumers=0;
    T *desc=nullptr;
  };

  struct rxring_state {

    vmxnet3_rx_desc* desc0 = nullptr;
    vmxnet3_rx_desc* desc1 = nullptr;
    vmxnet3_rx_comp* comp  = nullptr;
    uint32_t desc0_size = NUM_RX_DESC1;
    uint32_t desc1_size = NUM_RX_DESC2;
    int index = 0;
    uint16_t id0;
    uint16_t id1;
    RingData<vmxnet3_rx_desc> rings[2];
    RingData<vmxnet3_rx_comp> compRing;
  };

  void refill(rxring_state&);

  bool     check_version();
  uint16_t check_link();
  bool     reset();
  uint32_t command(uint32_t cmd);
  void     retrieve_hwaddr();
  void     set_hwaddr(MAC::Addr&);

  hw::PCI_Device& m_pcidev;
  std::vector<uint8_t> irqs;
  uintptr_t     iobase = 0;
  uintptr_t     ptbase = 0;
  MAC::Addr     hw_addr;
  uint16_t      m_mtu  = 0;
  vmxnet3_dma*  dma = nullptr;

  ring_stuff tx;
  rxring_state rx[NUM_RX_QUEUES];
  // deferred transmit dma
  uint8_t  deferred_irq  = 0;
  bool     deferred_kick = false;
  bool   already_polling = false;
  bool     link_state_up = false;
  static void handle_deferred();

  // sendq as double-ended q
  uint32_t& stat_sendq_cur;
  uint32_t& stat_sendq_max;
  uint64_t& stat_rx_refill_dropped;
  uint64_t& stat_sendq_dropped;
  std::deque<net::Packet_ptr> sendq;
  net::BufferStore bufstore_;
};

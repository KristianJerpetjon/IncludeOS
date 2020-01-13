// This file is a part of the IncludeOS unikernel - www.includeos.org
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

#pragma once
#ifndef HW_INTERRUPT_CONTROLLER_HPP
#define HW_INTERRUPT_CONTROLLER_HPP

#include <hw/device.hpp>
#include <delegate>
#include <arch.hpp>

namespace hw
{
  using InterruptCallback = delegate<void()>;

  class InterruptController : public Device {

  public:

    //construct an array for interrupt handlers
    InterruptController(uint32_t max,uint32_t spurious) :
      callbacks(new InterruptCallback[max]) {}

    /**
     * Method to get the type of device
     *
     * @return The type of device as a C-String
     */
    Device::Type device_type() const noexcept override
    { return Device::Type::InterruptController; }

    /**
     * Method to get the name of the device
     *
     * @return The name of the device as a std::string
     */
    virtual std::string device_name() const override = 0;

    /**
     * Method to deactivate the Interrupt Controller
     */
    virtual void deactivate() override = 0;

    void interrupt() noexcept
    {
      //do while not spurious
      uint32_t irq;
      //this only works if we disable and or clear the irq received..
      //prelim asumption .. leave this to the handler ?..
    //  while((irq=pending() != spurious())
      irq=pending();
      disable(irq);
      {
        if (callbacks.get()[irq] != nullptr)
        {
          //disable enable irq.
          callbacks.get()[irq]();
        }
        //issue deferred event.

      }
      //missing some virtual mapping ?
      trigger_event(irq);

    } //adds indirection
    virtual void enable(uint32_t irq) noexcept =0;
    virtual void disable(uint32_t irq) noexcept =0;

    virtual uint32_t pending()=0;

    uint32_t irq_max() const noexcept { return m_irq_max; } //highest possible irq number
    uint32_t spurious()const noexcept { return m_spurious; } //what irq number is spurious
    //pointer to direct handlers..
    void setIrqHandler(uint32_t irq,InterruptCallback dc) noexcept
    {
      if (irq <= irq_max())
        callbacks.get()[irq]=dc;
    }
    void removeIrqHandler(uint32_t irq) noexcept
    {
      if (irq <= irq_max())
        callbacks.get()[irq]=nullptr;
    }
  protected:
    virtual uint32_t irq_decode();

  private:
    void trigger_event(uint32_t id) noexcept
    {//whats the correct answer here ?
      Events::get().trigger_event(id);
    //  Events::get().trigger_event(vector - IRQ_BASE);
    }
    uint32_t m_irq_max;
    uint32_t m_spurious;
    std::unique_ptr<InterruptCallback[]> callbacks;

    //list or something else.
  };
}
#endif//HW_INTERRUPT_CONTROLLER_HPP

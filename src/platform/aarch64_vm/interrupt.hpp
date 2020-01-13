#ifndef INTERRUPT_HPP
#define INTERRUPT_HPP

#include <delegate>
#include <device>

namespace armv8
{
  using InterruptCallback = delegate<void()>;

  class InterruptHandler : public Device
  {
  public:
    InterruptHandler(uint32_t max,uint32_t spurious) : callbacks(new InterruptCallback[max]) {}
    virtual void handle_interrupt()
    {
      //do while not spurious
      uint32_t irq=pending();
      if (callbacks.get()[irq] != nullptr)
      {
        callbacks.get()[irq]();
      }

    } //adds indirection
    virtual void enable(uint32_t irq)=0;
    virtual void disable(uint32_t irq)=0;
    virtual uint32_t pending()=0;
    uint32_t irq_max(){ return m_irq_max; } //highest possible irq number
    uint32_t spurious(){ return m_spurious; } //what irq number is spurious
    //pointer to direct handlers..
    void setIrqHandler(uint32_t irq,InterruptCallback dc)
    {
      if (irq <= irq_max())
        callbacks.get()[irq]=dc;
    }
    void removeIrqHandler(uint32_t irq)
    {
      if (irq <= irq_max())
        callbacks.get()[irq]=nullptr;
    }
  protected:
    virtual uint32_t irq_decode();

  private:
    uint32_t m_irq_max;
    uint32_t m_spurious;
    std::unique_ptr<InterruptCallback[]> callbacks;

    //list or something else.
  };



  //should be called by interrupt handler ?
//  static void handle_interrupt(uint32_t interrupt);
};

#endif// INTERRUPT_HPP

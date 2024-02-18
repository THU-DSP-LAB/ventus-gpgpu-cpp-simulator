#ifndef UTILS_H_
#define UTILS_H_

#include "systemc.h"

template <typename T>
class IO_in_if : virtual public sc_interface
{
public:
    virtual void read(T &data) = 0;
    virtual bool is_ready() const = 0;
    virtual bool is_valid() const = 0;
    virtual void set_ready(bool ready) = 0;
};

template <typename T>
class IO_out_if : virtual public sc_interface
{
public:
    virtual void write(const T &data) = 0;
    virtual bool is_ready() const = 0;
    virtual bool is_valid() const = 0;
    virtual void set_valid(bool valid) = 0;
};

template <typename T>
class DecoupledIO : public sc_channel, public IO_in_if<T>, public IO_out_if<T>
{
    sc_signal<T> bits;
    sc_signal<bool> ready;
    sc_signal<bool> valid;

public:
    DecoupledIO(sc_module_name name) : sc_channel(name),
                                       bits((std::string(name) + "_bits").c_str()),
                                       ready((std::string(name) + "_ready").c_str()),
                                       valid((std::string(name) + "_valid").c_str())
    {
        // 初始化信号值
        ready.write(false);
        valid.write(false);
    }

    void read(T &data) override { data = bits.read(); }
    void write(const T &data) override { bits.write(data); }

    bool is_ready() const override { return ready.read(); }
    bool is_valid() const override { return valid.read(); }

    void set_ready(bool r) override { ready.write(r); }
    void set_valid(bool v) override { valid.write(v); }
};

#endif

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

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

struct TaskInfo {   // every task contains multiple kernels that run sequentially
    std::string name;
    std::queue<std::string> kernels;
    int nWarps;
    int simCycles;
};

std::map<std::string, TaskInfo> load_tasks_from_ini(const std::string& ini_file, const std::vector<std::string>& task_names) {
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(ini_file, pt);

    std::map<std::string, TaskInfo> tasks;
    std::set<std::string> task_set(task_names.begin(), task_names.end()); // 将task_names转换为set便于查找，所以目前不支持重复的task_name，且kernel包含task_name区分所属task

    for (const auto& task_name : task_names) {
        if (pt.find(task_name) != pt.not_found()) { // 检查.ini文件中是否存在该task
            TaskInfo task;
            task.name = task_name;
            task.nWarps = pt.get<int>(task_name + ".nWarps", 0);
            task.simCycles = pt.get<int>(task_name + ".SimCycles", 0);
            std::string files = pt.get<std::string>(task_name + ".Files");
            std::istringstream ss(files);
            std::string file;
            while (getline(ss, file, ',')) {
                task.kernels.push(file);
            }
            tasks[task_name] = task;
        }
    }
    return tasks;
}



#endif

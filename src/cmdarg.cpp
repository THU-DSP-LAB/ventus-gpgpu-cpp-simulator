#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using f_new_kernel_t = std::function<
    int(std::string name, std::string metafile, std::string datafile, bool add_to_task)>;

int cmdarg_kernel(std::string arg, f_new_kernel_t new_kernel);
int cmdarg_task(std::string arg, std::function<int(std::string name)> new_task);
int cmdarg_error(std::vector<std::string> args);
int cmdarg_help(int exit_id);

int parse_arg(
    std::vector<std::string> args, uint64_t& sim_time, f_new_kernel_t new_kernel,
    std::function<int(std::string name)> new_task
) {
    for (int argid = 0; argid < args.size(); argid++) {
        if (args[argid] == "-f") {
            if (++argid >= args.size()) {
                cmdarg_error(std::vector<std::string>(args.begin() + argid - 1, args.end()));
            } else {
                std::filesystem::path filename, path_to_file, path_origin;
                try {
                    filename = std::filesystem::canonical(args[argid]);
                } catch (const std::filesystem::filesystem_error& e) {
                    std::cout << "Error: file not found: -f " << args[argid] << "\n"
                              << e.what() << std::endl;
                    exit(1);
                }
                path_to_file = filename.parent_path();
                path_origin = std::filesystem::current_path();
                std::filesystem::current_path(path_to_file);
                std::ifstream file(filename);
                if (!file.is_open()) {
                    std::cout << "Error: file cannot open: -f " << args[argid] << std::endl;
                    exit(1);
                }
                std::vector<std::string> arguments;
                std::string line;
                while (std::getline(file, line)) {
                    // Ignore characters following '#' on each line
                    size_t sharp_pos = line.find('#');
                    if (sharp_pos != std::string::npos) {
                        line.erase(sharp_pos);
                    }
                    // Split the line into arguments based on spaces or other delimiters
                    std::istringstream iss(line);
                    std::string arg;
                    while (iss >> arg) {
                        arguments.push_back(arg);
                    }
                }
                parse_arg(arguments, sim_time, new_kernel, new_task);
                std::filesystem::current_path(path_origin);
            }
        } else if (args[argid] == "--task") {
            if (++argid >= args.size()) {
                cmdarg_error(std::vector<std::string>(args.begin() + argid - 1, args.end()));
            } else { // TODO
                if (cmdarg_task(args[argid], new_task)) {
                    cmdarg_error(
                        std::vector<std::string>(args.begin() + argid - 1, args.begin() + argid + 1)
                    );
                }
            }
        } else if (args[argid] == "--kernel") {
            if (++argid >= args.size()) {
                cmdarg_error(std::vector<std::string>(args.begin() + argid - 1, args.end()));
            } else if (new_kernel) {
                if (cmdarg_kernel(args[argid], new_kernel)) {
                    cmdarg_error(
                        std::vector<std::string>(args.begin() + argid - 1, args.begin() + argid + 1)
                    );
                }
            }
        } else if (args[argid] == "--help") {
            cmdarg_help(0);
        } else if (args[argid] == "--sim-time-max" || args[argid] == "--sim-time") {
            if (++argid >= args.size()) {
                cmdarg_error(std::vector<std::string>(args.begin() + argid - 1, args.end()));
            } else {
                uint64_t simtime = std::stoull(args[argid]);
                if (simtime <= 0) {
                    std::cout << "Error: --sim-time-max needs number > 0\n";
                    cmdarg_error(std::vector<std::string>(args.begin() + argid - 1, args.end()));
                }
                sim_time = simtime;
            }
        } else {
            cmdarg_error(std::vector<std::string>(args.begin() + argid, args.begin() + argid + 1));
        }
    }
    return 0;
}

int cmdarg_task(std::string arg_raw, std::function<int(std::string name)> new_task) {
    int len = arg_raw.size();
    char* arg = new char[len + 1];
    strcpy(arg, arg_raw.c_str());

    char* name = nullptr;

    char* ptr1 = NULL;
    char* subarg = strtok_r(arg, ",", &ptr1);
    while (subarg) {
        if (strlen(subarg) > 0) {
            char* ptr2 = NULL;
            char* var = strtok_r(subarg, "=", &ptr2);
            char* val = strtok_r(NULL, "=", &ptr2);
            assert(var && val);

            if (strcmp(var, "name") == 0) {
                name = val;
            } else {
                goto RET_ERR;
            }
        }
        subarg = strtok_r(NULL, ",", &ptr1);
    }

    if (name == nullptr) {
        goto RET_ERR;
    }

    if (new_task(name)) {
        goto RET_ERR;
    }
    delete[] arg;
    return 0;

RET_ERR:
    delete[] arg;
    return -1;
}

int cmdarg_kernel(std::string arg_raw, f_new_kernel_t new_kernel) {
    int len = arg_raw.size();
    char* arg = new char[len + 1];
    strcpy(arg, arg_raw.c_str());

    bool add_to_task = false;
    char* name = nullptr;
    char* metafile = nullptr;
    char* datafile = nullptr;
    std::string metafile_abspath, datafile_abspath;

    char* ptr1 = NULL;
    char* subarg = strtok_r(arg, ",", &ptr1);
    while (subarg) {
        if (strlen(subarg) > 0) {
            char* ptr2 = NULL;
            char* var = strtok_r(subarg, "=", &ptr2);
            char* val = strtok_r(NULL, "=", &ptr2);
            assert(var);

            if (strcmp(var, "task") == 0) {
                add_to_task = true;
            } else if (strcmp(var, "name") == 0) {
                name = val;
            } else if (strcmp(var, "metafile") == 0) {
                metafile = val;
            } else if (strcmp(var, "datafile") == 0) {
                datafile = val;
            } else {
                goto RET_ERR;
            }
        }
        subarg = strtok_r(NULL, ",", &ptr1);
    }

    if (!(name && metafile && datafile)) {
        goto RET_ERR;
    }

    try {
        metafile_abspath = std::filesystem::canonical(metafile);
        datafile_abspath = std::filesystem::canonical(datafile);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cout << "Error: file not found: \n"
                  << "metafile = " << metafile << "\ndatafile = " << datafile << "\n"
                  << e.what() << std::endl;
        goto RET_ERR;
    }

    if (new_kernel(name, metafile_abspath, datafile_abspath, add_to_task)) {
        goto RET_ERR;
    }
    delete[] arg;
    return 0;

RET_ERR:
    delete[] arg;
    return -1;
}

int cmdarg_error(std::vector<std::string> args) {
    std::cout << "Incorrect argument: \n";
    for (int i = 0; i < args.size(); i++) {
        std::cout << "  " << args[i] << "\n";
    }
    cmdarg_help(1);
    exit(1);
}

int cmdarg_help(int exit_id) {
    std::cout
        << "ventus-sim [--arg subarg1=val1,subarg2=val2,...]\n"
        << "\n"
        << "Supported cmdline arguments: \n"
        << "-f         FILE     string  // load cmd args from file\n"
        << "                            // if no cmd args is given, -f ventus_cmdargs.txt is "
           "applied\n"
        << "\n"
        << "--task                      // create a new GPGPU task\n"
        << "  subarg:  name     string  // 任取\n"
        //<< "           id       uint    // 任取\n"
        << "\n"
        << "--kernel                    // create a new GPGPU kernel\n"
        << "  subarg:  name     string  // 任取\n"
        << "           metafile string  // kernel的.metadata文件路径\n"
        << "           datafile string  // kernel的.data文件路径\n"
        << "           task             // "
           "若有则归属上一个声明的task，若无则为不归属任何task的独立kernel\n"
        << "\n"
        //<< "--snapshot INTERVAL uint    // 每隔多少仿真时间生成一个快照，若为0则关闭快照功能\n"
        << "--sim-time NUM      uint    // number of simulation cycles" << std::endl;
    exit(exit_id);
}

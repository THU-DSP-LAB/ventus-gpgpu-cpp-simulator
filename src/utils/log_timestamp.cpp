#include <cstdio>
#include <systemc.h>

extern "C" {

void log_get_timestamp(char *buf, int len){
    auto time = sc_time_stamp();
    snprintf(buf, len, "%s: ", time.to_string().c_str());
}

}

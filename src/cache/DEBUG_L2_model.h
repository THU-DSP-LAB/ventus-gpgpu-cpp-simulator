#ifndef DEBUG_L2_MODEL_H
#define DEBUG_L2_MODEL_H

#include "l1_data_cache.h"

struct DEBUG_L2_memRsp :public pipe_reg_base, public L2_2_dcache_memRsp{
    DEBUG_L2_memRsp(){}

    void update_with(L2_2_dcache_memRsp memRsp){
    d_opcode = memRsp.d_opcode;
    d_source = memRsp.d_source;
    d_mask = memRsp.d_mask;
    d_data = memRsp.d_data;
    set_valid();
    }
};

class DEBUG_L2_model : public cache_building_block{
public:
    DEBUG_L2_model(){
        for(int i = 0;i<m_L2_data_array.size();++i){
            m_L2_data_array[i] = i*10;
        }
    }

    DEBUG_L2_model(int verbose_level):m_DEBUG_verbose_level(verbose_level){
        for(int i = 0;i<m_L2_data_array.size();++i){
            m_L2_data_array[i] = i*10;
        }
    }

    bool return_Q_is_empty(){
        return m_return_Q.empty();
    }

    L2_2_dcache_memRsp DEBUG_serial_pop(){
        assert(!m_return_Q.empty());
        auto value = m_return_Q.front();
        m_return_Q.pop_front();
        return value;
    }

    //非随机，一直提供次次尾部的元素
    L2_2_dcache_memRsp DEBUG_random_pop(){
        std::deque<L2_2_dcache_memRsp>::iterator value_iter;
        if (m_return_Q.size()<=2){
            value_iter = m_return_Q.begin();
        }else{
            value_iter = m_return_Q.begin() + 2;
        }
        auto value = *value_iter;
        m_return_Q.erase(value_iter);
        return value;
    }

    void DEBUG_L2_memReq_process(dcache_2_L2_memReq req, cycle_t time){
        if (!m_process_Q[m_minimal_process_latency-1].is_valid()){
            enum TL_UH_D_opcode return_op;
            cache_line_t return_data{};
            std::array<bool,LINEWORDS> return_mask{};
            if(req.a_opcode == PutFullData || (req.a_opcode == PutPartialData && req.a_param == 0x0)){
                return_op = AccessAck;//常规写入缺失、替换写回、冲刷
            }else{
                return_op = AccessAckData;
                if (req.a_opcode == Get){//常规读出缺失
                    if(req.a_param == 0x0){
                        return_mask.fill(true);
                        assert((req.a_address + LINEWORDS < m_L2_capacity) && "L1 memReq Get请求的缓存行地址超出受测L2模型范围");
                        auto it_base = m_L2_data_array.begin()+req.a_address;
                        std::copy(it_base,it_base+LINEWORDS,return_data.begin());
                    }else{//LR
                        assert((req.a_param == 0x1) && "非法Get，在L2被逮捕");
                        return_mask[0] = true;
                        return_data[0] = m_L2_data_array[req.a_address];
                    }
                }else if(req.a_opcode == PutPartialData && req.a_param == 0x1){//SC
                    return_mask[0] = true;
                    return_data[0] = 0x0;//0x0表示成功，0x1表示失败
                }else{
                    assert(false && "L2处理到非法memReq请求");
                }
            }
            L2_2_dcache_memRsp new_memRsp = L2_2_dcache_memRsp(return_op,req.a_source,return_mask,return_data);
            m_process_Q[m_minimal_process_latency-1].update_with(new_memRsp);

            //debug info
            if(m_DEBUG_verbose_level>=1){
                std::cout << std::setw(5) << time << " | memReq |";
                std::cout <<"   |" << std::setw(3) << req.a_source;
                std::cout << "| a_op=" << req.a_opcode << std::endl;
            }
        }
    }

    void cycle(){
        if (m_process_Q[0].is_valid()){
            m_return_Q.push_back(m_process_Q[0]);
        }
        for (unsigned stage = 0; stage < m_minimal_process_latency - 1; ++stage){
            m_process_Q[stage] = m_process_Q[stage + 1];
            m_process_Q[stage + 1].invalidate();
        }
    }

    void print_config_summary(){
        std::cout << "最小处理周期：" << m_minimal_process_latency << std::endl;
        std::cout << "L2容量（word）：" << m_L2_capacity << std::endl;
    }
private:
    //从L2 memReq到L2 memRsp的最小间隔周期
    constexpr static int m_minimal_process_latency = 3;
    constexpr static int m_L2_capacity = 64;
    std::array<DEBUG_L2_memRsp,m_minimal_process_latency> m_process_Q {};
    std::deque<L2_2_dcache_memRsp> m_return_Q;
    std::array<u_int32_t,m_L2_capacity> m_L2_data_array;

    int m_DEBUG_verbose_level=1;
};

#endif
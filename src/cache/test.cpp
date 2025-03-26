#include "l1_data_cache.h"
#include "DEBUG_L2_model.h"
#include <iostream>
#include <string>
#include <fstream>
//#include <sstream>
#include <regex>

class test_env : cache_building_block{

public:
    test_env(){
        //DEBUG_init_stimuli();
    }

    test_env(int verbose){
        verbose_level = verbose;
        dcache = l1_data_cache(verbose);
        L2 = DEBUG_L2_model(verbose);
    }

    test_env(bool dump_csv){
        m_dump_csv = dump_csv;
    }

    test_env(int verbose, bool dump_csv){
        verbose_level = verbose;
        m_dump_csv = dump_csv;
        dcache = l1_data_cache(verbose);
        L2 = DEBUG_L2_model(verbose);
    }

    void print_config_summary(){
        std::cout << "**** ref L2 configuration summary ****" << std::endl;
        L2.print_config_summary();

        std::cout << "**** cache under test configuration summary ****" << std::endl;
        std::cout << "NLANE=" << NLANE << std::endl;
        std::cout << "NSET=" << NSET << "; NWAY=" << NWAY << std::endl;
        std::cout << "LINEWORDS=" << LINEWORDS << std::endl;
        std::cout << "N_MSHR_ENTRY=" << N_MSHR_ENTRY << std::endl;
        std::cout << "N_MSHR_SUBENTRY=" << N_MSHR_SUBENTRY << std::endl;
        std::cout << "N_MSHR_SPECIAL_ENTRY=" << N_MSHR_SPECIAL_ENTRY << std::endl;
        if(verbose_level>=4){
            std::cout << "CORE_RSP_Q_DEPTH=" << CORE_RSP_Q_DEPTH << std::endl;
            std::cout << "MEM_REQ_Q_DEPTH=" << MEM_REQ_Q_DEPTH << std::endl;
            std::cout << "MEM_RSP_Q_DEPTH=" << MEM_RSP_Q_DEPTH << std::endl;
        }
    }

    void DEBUG_print_coreRsp_pop(cycle_t time){
        if (dcache.m_coreRsp_Q.m_Q.size() != 0){
            if(verbose_level>=1){
                dcache.m_coreRsp_Q.DEBUG_print(time);
            }
            dcache.m_coreRsp_Q.m_Q.pop_front();
        }
        //dcache.m_coreRsp_ready = true;
    }

    void DEBUG_cycle(cycle_t time, std::ifstream& instr_file_name, std::ofstream& waveform_file){
        DEBUG_cycle(time,instr_file_name);
        waveform_file << time << ",";
        DEBUG_waveform_a_cycle(waveform_file);
    }

    void DEBUG_cycle(cycle_t time, std::ifstream& instr_file_name){
        std::string instruction;

        DEBUG_print_coreRsp_pop(time);
        L2.cycle();
        if(dcache.m_memReq_pipe3_reg.is_valid()){
            L2.DEBUG_L2_memReq_process(dcache.m_memReq_pipe3_reg, time);
            dcache.m_memReq_pipe3_reg.invalidate();
        }
        dcache.cycle(time);
        if(!L2.return_Q_is_empty()){
            dcache.m_memRsp_Q.m_Q.push_back(L2.DEBUG_serial_pop());
        }
        if(!dcache.m_coreReq.is_valid() && !instr_file_name.eof()){
            getline(instr_file_name, instruction);
            LSU_2_dcache_coreReq coreReq;
            if(parse_instruction(instruction,coreReq,time)){
                dcache.m_coreReq.update_with(coreReq);
            }//no else, just skip this cycle coreReq
        }
    }

    bool parse_instruction(std::string instruction, LSU_2_dcache_coreReq& coreReq, cycle_t time){
        std::regex field_regex("\\s*(\\S+)\\s+(\\S+)\\s*");
        //std::regex opcode_regex("([^\\.]+)(?:\\.(\\S+))*");
        //std::regex reg_imm_regex("(\\S+)(?:\\,(\\S+))*");
        std::smatch field_match;
        if (!regex_match(instruction, field_match, field_regex)) {
            if(verbose_level>=2){
                std::cout <<"周期"<< time << "，空行或非法指令："<< instruction << std::endl;
            }
            return false;
        }
        if(verbose_level>=4){
            std::cout << "周期"<< time << "，opcode："<< field_match[1].str() << "。寄存器字段：" ;//<< field_match[2].str() << std::endl;
        }
        std::string opcode = field_match[1].str();
        /*std::vector<std::string> opcode_fields;
        std::string opcode_field_temp;
        std::stringstream opcode_ss(opcode);
        while(getline(opcode_ss, opcode_field_temp, '.')){
            opcode_fields.push_back(opcode_field_temp);
            if(verbose_level>=2){
                std::cout << opcode_fields.back() << " | " ;
            }
        }*/
        std::string reg_imm = field_match[2].str();
        std::array<std::string,4> reg_imm_fields;
        std::string reg_imm_temp;
        std::stringstream reg_imm_ss(reg_imm);
        int field_cnt=0;
        while(getline(reg_imm_ss, reg_imm_temp, ',')){
            reg_imm_fields[field_cnt]=reg_imm_temp;
            if(verbose_level>=4){
                std::cout << reg_imm_temp << " | " ;
            }
            ++field_cnt;
        }
        if(verbose_level>=4){std::cout << std::endl;}

        enum LSU_cache_coreReq_opcode coreReq_opcode;
        uint32_t coreReq_type;
        u_int32_t coreReq_wid;
        u_int32_t coreReq_id;
        u_int32_t coreReq_word_addr;
        vec_nlane_t p_addr{};
        std::array<bool,NLANE> p_mask{};
        p_mask.fill(false);
        vec_nlane_t p_data{};

        if(opcode == "lb" || opcode == "lh" || opcode == "lw" ||
        opcode == "lr.w" || opcode == "vle32.v"){
            coreReq_opcode = Read;
            if (opcode == "lr.w"){
                coreReq_type = 1;
            }else{
                coreReq_type = 0;
            }
            coreReq_wid = random(0,31);
            coreReq_id = cast_regidx_to_int(reg_imm_fields[0]);
            coreReq_word_addr = cast_addr_to_int(reg_imm_fields[1]);
            if(opcode == "lb" || opcode == "lh" || opcode == "lw" || opcode == "lr.w"){
                p_addr[0] = get_block_offset(coreReq_word_addr);
                p_mask[0] = true;
            }else{
                if(opcode == "vle32.v"){
                    for(int i = 0;i<NLANE;++i){
                        assert((get_block_offset(coreReq_word_addr) == 0 ) && "LSU-d$之前，向量访问需要block对齐");
                        p_addr[i] = i;
                    }
                    p_mask.fill(true);
                }
            }
            
            if(verbose_level>=2){
                std::cout <<"周期"<< time << "，Load, op = " << opcode << std::endl;
            }
        }else if(opcode == "sb" || opcode == "sh" || opcode == "sw" || 
        opcode == "sc.w" || opcode == "vse32.v" ){//|| opcode == "vsse32.v" || 
        //opcode == "vsuxe32.v" || opcode == "vsoxe32.v"){
            coreReq_opcode = Write;
            if (opcode == "sc.w"){
                coreReq_type = 1;
            }else{
                coreReq_type = 0;
            }
            coreReq_wid = random(0,31);
            if(opcode == "sc.w"){
                coreReq_id = cast_regidx_to_int(reg_imm_fields[0]);
                coreReq_word_addr = cast_addr_to_int(reg_imm_fields[2]);
                reg_imm_fields[0] = reg_imm_fields[1];//data
            }else{
                coreReq_id = 88;
                coreReq_word_addr = cast_addr_to_int(reg_imm_fields[1]);
            }
            
            if(opcode == "sb" || opcode == "sh" || opcode == "sw" || opcode == "sc.w"){
                p_addr[0] = get_block_offset(coreReq_word_addr);
                p_mask[0] = true;
                p_data[0] = std::stoi(reg_imm_fields[0]);
            }else{
                if(opcode == "vse32.v"){
                    assert((get_block_offset(coreReq_word_addr) == 0 ) && "LSU-d$之前，向量访问需要block对齐");
                    u_int32_t data_base = std::stoi(reg_imm_fields[0]);
                    for(int i = 0;i<NLANE;++i){
                        p_addr[i] = i;
                        p_data[i] = data_base + i;//不通用，仅用于测试
                    }
                    p_mask.fill(true);
                }
            }

            if(verbose_level>=2){
                std::cout <<"周期"<< time << "，Store, op = " << opcode << std::endl;
            }
        }else if(opcode == "fence"){
            std::string t0 = reg_imm_fields[0];
            std::string t1 = reg_imm_fields[1];
            coreReq_opcode = InvOrFlu;
            coreReq_wid = random(0,31);
            coreReq_id = 66;
            if ((t0=="r"&&t1=="r") || (t0=="r"&&t1=="rw") || (t0=="w"&&t1=="r") ||
            (t0=="w"&&t1=="rw") || (t0=="rw"&&t1=="r") || (t0=="rw"&&t1=="rw")){
                coreReq_type = 0x0;
            }else if((t0=="w"&&t1=="w") || (t0=="rw"&&t1=="w")){
                coreReq_type = 0x1;
            }else if((t0=="r"&&t1=="w")){
                coreReq_type = 0x2;
            }else{
                if(verbose_level>=2){
                    std::cout <<"周期"<< time << "，非法FENCE： " << instruction << std::endl;
                }
                return false;
            }

            if(verbose_level>=2){
                std::cout <<"周期"<< time << "，Fence, op = " << opcode << std::endl;
            }
        }else if(opcode[0] == 'a' && opcode[1] == 'm' && opcode[2] == 'o'){
            coreReq_opcode = Amo;
            coreReq_type = amoswap;//不通用，仅用于测试
            coreReq_wid = random(0,31);
            coreReq_id = cast_regidx_to_int(reg_imm_fields[0]);
            coreReq_word_addr = cast_addr_to_int(reg_imm_fields[2]);
            p_addr[0] = get_block_offset(coreReq_word_addr);
            p_mask[0] = true;
            p_data[0] = std::stoi(reg_imm_fields[1]);

            if(verbose_level>=2){
                std::cout <<"周期"<< time << "，AMO, op = " << opcode << std::endl;
            }
        }else{
            if(verbose_level>=2){
                std::cout <<"周期"<< time << "，非法指令："<< instruction << std::endl;
            }
            return false;
        }

        u_int32_t coreReq_block_idx = get_block_idx(coreReq_word_addr);

        coreReq = LSU_2_dcache_coreReq(coreReq_opcode,
            coreReq_type,
            coreReq_wid,
            coreReq_id,
            coreReq_block_idx,
            p_addr,
            p_mask,
            p_data);
        return true;
    }

    //输入：寄存器索引
    int cast_regidx_to_int(std::string reg_idx){
        assert(reg_idx[0]=='x' || reg_idx[0]=='v');
        std::string number = reg_idx.substr(1);
        return std::stoi(number);
    }

    //输入：括号中的地址
    int cast_addr_to_int(std::string addr_in_bracket) {
        assert(addr_in_bracket.front() == '(' && addr_in_bracket.back() == ')');
        addr_in_bracket = addr_in_bracket.substr(1);
        addr_in_bracket = addr_in_bracket.substr(0, addr_in_bracket.size() - 1);
        return std::stoi(addr_in_bracket);
    }

    void DEBUG_waveform_title(std::ofstream& waveform){
        waveform << "cycle," ;
        waveform << "cReq_st1_v,cReq_st1_wid,cRsp_st2_v,cRsp_st2_wid,mRsp_st1,mRsp_st2_d_source,";
        waveform << "cReq_v,cReq_op,cReq_type,cReq_wid,cReq_id,cReq_block_idx,cReq_block_offset_0,cReq_mask_0,cReq_mask_1,cReq_data_0" << "," ;
        waveform << "cRsp_v,cRsp_wid,cRsp_id,cRsp_mask_0,cRsp_wxd,cRsp_data_0" << ",";
        waveform << "d_v,d_op,d_source,d_mask_0,d_mask_1,d_data_0" << ",";
        waveform << "a_v,a_op,a_param,a_source,a_addr,a_mask_0,a_mask_1,a_data" << std::endl;
    }

    void DEBUG_waveform_a_cycle(std::ofstream& waveform){
        dcache.DEBUG_waveform_a_cycle(waveform);
        waveform << std::endl;
    }

    l1_data_cache dcache;
private:
    //std::deque<LSU_2_dcache_coreReq> coreReq_stimuli;
    DEBUG_L2_model L2;
    /*
    verbose_level = 0: 无任何打印信息
    verbose_level = 1: 重要打印信息
    verbose_level = 2: 主要
    */
    int verbose_level=1;
    bool m_dump_csv=true;
};

/*仿真中生成波形时一个周期内的信号写在同一行，
为了波形文件的可读性，一个周期最好位于同一列
本函数调用shell脚本对csv波形文件进行转置*/
void shell_csv_transpose(){
    std::string command = "./csv_transpose.sh waveform_result.csv";

    int result = system(command.c_str());

    if (result != 0) {
        std::cerr << "调用转置脚本失败！" << std::endl;
        return;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) { // 检查命令行参数是否正确
        std::cerr << "Usage: " << argv[0] << " input_file" << std::endl;
        return 1;
    }
    std::ifstream infile(argv[1]);
    if (!infile) { // 检查文件是否打开成功
        std::cerr << "Error: cannot open file " << argv[1] << std::endl;
        return 1;
    }

    std::cout << "modeling cache now" << std::endl;
    //仿真参数配置
    bool dump_csv = true;
    int debug_info_verbose_level = 1;

    //仿真环境初始化
    test_env tb(debug_info_verbose_level,dump_csv);
    tb.print_config_summary();
    std::ofstream waveform("waveform_result.csv");
    if (dump_csv){
        tb.DEBUG_waveform_title(waveform);
    }
    //tb.dcache.m_tag_array.DEBUG_random_initialize(100);
    //tb.dcache.m_tag_array.DEBUG_visualize_array(28,4);
    if(debug_info_verbose_level>=1){
        std::cout << std::endl << " time | event  |wid|src|" << std::endl;
    }

    //仿真开始
    for (int i = 100 ; i < 130 ; ++i){
        if (dump_csv){
            tb.DEBUG_cycle(i,infile,waveform);
        }else{
            tb.DEBUG_cycle(i,infile);
        }
    }
    
    //仿真结束
    if(debug_info_verbose_level>=1){
        tb.dcache.m_tag_array.DEBUG_visualize_array();
    }

    waveform.close();
    infile.close();
    shell_csv_transpose();
    return 0;
}
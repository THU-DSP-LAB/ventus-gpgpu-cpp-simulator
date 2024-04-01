#include "context_model.hpp"

uint32_t kernel_info_t::getBufferData(unsigned int virtualAddress, bool &addrOutofRangeException, const I_TYPE &ins)
{
    addrOutofRangeException = 0;
    int bufferIndex = -1;
    for (int i = 0; i < m_metadata.num_buffer; i++)
    {
        // std::cout << std::hex << "getBufferData: ranging from " << buffer_base[i] << " to " << (buffer_base[i] + buffer_size[i]) << ", virtualAddr=" << virtualAddress << std::dec << "\n";
        if (virtualAddress >= m_metadata.buffer_base[i] && virtualAddress < (m_metadata.buffer_base[i] + m_metadata.buffer_allocsize[i]))
        {
            bufferIndex = i;
            break;
        }
    }

    if (bufferIndex == -1)
    {
        std::cerr << "getBufferData Error: No buffer found for the given virtual address 0x" << std::hex << virtualAddress
                  << " for ins pc=0x" << ins.currentpc << ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        addrOutofRangeException = 1;
        return 0;
    }

    int offset = virtualAddress - m_metadata.buffer_base[bufferIndex];
    // std::cout << "getBufferData: offset=" << std::hex << offset << "\n";
    int startIndex = offset;

    uint32_t data = 0;

    int bytesToRead = 0; // 将要读取的字节数

    // 确定读取的字节数
    if (ins.ddd.mem_whb == DecodeParams::MEM_W)
        bytesToRead = 4;
    else if (ins.ddd.mem_whb == DecodeParams::MEM_H)
        bytesToRead = 2;
    else if (ins.ddd.mem_whb == DecodeParams::MEM_B)
        bytesToRead = 1;

    for (int i = 0; i < bytesToRead; i++)
    {
        // std::cout << "getBufferData: fetching buffers[" << bufferIndex << "][" << (startIndex + i) << "], buffer size=" << buffers[bufferIndex].size() << "\n";
        uint8_t byte = (*m_buffer_data)[bufferIndex][startIndex + i];
        data |= static_cast<uint32_t>(byte) << (i * 8);
    }

    // 如果不是读取4个字节，则根据mem_unsigned来决定如何处理剩余的位
    if (bytesToRead < 4)
    {
        if (ins.ddd.mem_unsigned == 1)
        {
            // 无需操作，data已正确设置
        }
        else
        {
            // 符号位扩展
            int shift = (4 - bytesToRead) * 8;
            int32_t signExtension = (static_cast<int32_t>(data) << shift) >> shift;
            data = static_cast<uint32_t>(signExtension);
        }
    }

    return data;
}

void kernel_info_t::writeBufferData(int writevalue, unsigned int virtualAddress, const I_TYPE &ins)
{
    int bufferIndex = -1;
    for (int i = 0; i < m_metadata.num_buffer; i++)
    {
        if (virtualAddress >= m_metadata.buffer_base[i] &&
            virtualAddress < (m_metadata.buffer_base[i] + m_metadata.buffer_allocsize[i]))
        {
            bufferIndex = i;
            break;
        }
    }

    if (bufferIndex == -1)
    {
        std::cerr << "writeBufferData Error: No buffer found for the given virtual address 0x" << std::hex << virtualAddress << " for ins" << ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        return;
    }

    int offset = virtualAddress - m_metadata.buffer_base[bufferIndex];
    int startIndex = offset;

    int bytesToWrite = 0; // 将要写入的字节数
    if (ins.ddd.mem_whb == DecodeParams::MEM_W)
        bytesToWrite = 4;
    else if (ins.ddd.mem_whb == DecodeParams::MEM_H)
        bytesToWrite = 2;
    else if (ins.ddd.mem_whb == DecodeParams::MEM_B)
        bytesToWrite = 1;

    for (int i = 0; i < 4; i++)
    {
        uint8_t byte = static_cast<uint8_t>(writevalue >> (i * 8));
        (*m_buffer_data)[bufferIndex][startIndex + i] = byte;
    }

    // std::cout << "SM" << sm_id << std::hex << " write extmem[" << virtualAddress << "]=" << writevalue << std::dec << ",ins=" << ins << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
}

uint32_t kernel_info_t::readInsBuffer(unsigned int virtualAddr, bool &addrOutofRangeException)
{
    addrOutofRangeException = 0;
    int startIndex = virtualAddr - m_metadata.startaddr;
    if (startIndex < 0 || startIndex > m_metadata.buffer_allocsize[m_metadata.insBufferIndex])
    {
        std::cout << "readInsBuffer Error: virtualAddr(pc)=0x" << std::hex << virtualAddr << std::dec << " at " << sc_time_stamp() << "," << sc_delta_count_at_current_time() << "\n";
        addrOutofRangeException = 1;
        return 0;
    }
    uint32_t data = 0;
    for (int i = 0; i < 4; i++)
    {
        uint8_t byte = (*m_buffer_data)[m_metadata.insBufferIndex][startIndex + i];
        data |= static_cast<uint32_t>(byte) << (i * 8);
    }
    return data;
}

#ifndef CONTEXT_MODEL_H_
#define CONTEXT_MODEL_H_

#include "parameters.h"
#include <memory>

struct dim3
{
    uint64_t x, y, z;
};

void increment_x_then_y_then_z(dim3 &i, const dim3 &bound);

struct meta_data_t
{ // 这个metadata是供驱动使用的，而不是给硬件的
    uint64_t startaddr;
    uint64_t kernel_id;
    uint64_t kernel_size[3];    ///> 每个kernel的workgroup三维数目
    uint64_t wf_size;           ///> 每个warp的thread数目
    uint64_t wg_size;           ///> 每个workgroup的warp数目
    uint64_t metaDataBaseAddr;  ///> CSR_KNL的值，
    uint64_t ldsSize;           ///> 每个workgroup使用的local memory的大小
    uint64_t pdsSize;           ///> 每个thread用到的private memory大小
    uint64_t sgprUsage;         ///> 每个workgroup使用的标量寄存器数目
    uint64_t vgprUsage;         ///> 每个thread使用的向量寄存器数目
    uint64_t pdsBaseAddr;       ///> private memory的基址，要转成每个workgroup的基地址， wf_size*wg_size*pdsSize
    uint64_t num_buffer;        ///> buffer的数目，包括pc
    uint64_t *buffer_base;      ///> 各buffer的基址。第一块buffer是给硬件用的metadata
    uint64_t *buffer_size;      ///> 各buffer的size，以Bytes为单位。实际使用的大小，用于初始化.data
    uint64_t *buffer_allocsize; ///> 各buffer的size，以Bytes为单位。分配的大小

    int insBufferIndex; // 指令在哪一个buffer
};

class kernel_info_t
{
public:
    std::vector<std::vector<uint8_t>> *m_buffer_data;

    kernel_info_t(const std::string &task_name, const std::string &kernel_name, const std::string &metadata_file, const std::string &data_file, std::vector<std::vector<uint8_t>> *existing_buffers = nullptr)
        : m_task_name(task_name), m_kernel_name(kernel_name)
    {
        initMetaData(metadata_file);
        init_extmem(data_file);
        std::cout << "kernel " << kernel_name << " initialized, set grid_dim=" << m_grid_dim.x << m_grid_dim.y << m_grid_dim.z << "\n";
    }

    bool no_more_ctas_to_run() const
    {
        return (m_next_cta.x >= m_grid_dim.x ||
                m_next_cta.y >= m_grid_dim.y ||
                m_next_cta.z >= m_grid_dim.z);
    }
    uint32_t get_kid() { return kernelID; }
    std::string get_kname() { return m_kernel_name; }
    std::string get_tname() { return m_task_name; }
    dim3 get_next_cta_id() const { return m_next_cta; }
    unsigned get_next_cta_id_single() const
    {
        return m_next_cta.x + m_grid_dim.x * m_next_cta.y +
               m_grid_dim.x * m_grid_dim.y * m_next_cta.z;
    }
    void increment_cta_id()
    {
        increment_x_then_y_then_z(m_next_cta, m_grid_dim);
    }

    unsigned get_startaddr() { return m_metadata.startaddr; }
    unsigned get_num_buffer() { return m_metadata.num_buffer; }
    unsigned get_num_warp_per_cta() { return m_metadata.wg_size; }
    unsigned get_num_thread_per_warp() { return m_metadata.wf_size; }
    unsigned get_ldsSize_per_cta() { return m_metadata.ldsSize; }
    unsigned get_pdsSize_per_thread() { return m_metadata.pdsSize; }
    uint64_t get_pdsBaseAddr() { return m_metadata.pdsBaseAddr; }
    uint64_t get_metadata_baseaddr() { return m_metadata.metaDataBaseAddr; }

    // 通过虚拟地址获取对应缓冲区的数据并转换为整数
    uint32_t getBufferData(unsigned int virtualAddress, bool &addrOutofRangeException, const I_TYPE &ins);
    void writeBufferData(int writevalue, unsigned int virtualAddress, const I_TYPE &ins);
    uint32_t readInsBuffer(unsigned int virtualAddr, bool &addrOutofRangeException);

private:
    std::string m_task_name;
    std::string m_kernel_name;
    meta_data_t m_metadata;
    uint32_t kernelID;
    unsigned m_running_cta; // 当前正在运行的cta数量
    std::array<int, MAX_RUNNING_CTA_PER_KERNEL> m_cta_status_panel;

    void initMetaData(const std::string &filename)
    {
        std::vector<uint64_t> metadata;
        readHexFile(filename, 64, metadata);
        assignMetadata(metadata, m_metadata);
    }

    bool isHexCharacter(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    }

    int charToHex(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        else if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        else
            return -1; // Invalid character
    }

    void readHexFile(const std::string &filename, int itemSize, std::vector<uint64_t> &items)
    { // itemSize为每个数据的比特数，这里为64
        ifstream file(filename);

        if (!file)
        {
            std::cout << "Error opening file: " << filename << std::endl;
            return;
        }

        char c;
        int bits = 0;
        uint64_t value = 0;
        bool leftside = false;

        while (file.get(c))
        {
            if (c == '\n')
            {
                if (bits != 0)
                    leftside = true;
                continue;
            }

            if (!isHexCharacter(c))
            {
                std::cout << "Invalid character found: " << c << " in " << filename << std::endl;
                continue;
            }

            int hexValue = charToHex(c);
            if (leftside)
                value = value | ((uint64_t)hexValue << (92 - bits));
            else
                value = (value << 4) | hexValue;
            bits += 4;

            if (bits >= itemSize)
            {
                items.push_back(value);
                value = 0;
                bits = 0;
                leftside = false;
            }
        }

        if (bits > 0)
        {
            std::cout << "Warning: Incomplete item found at the end of the file!" << std::endl;
        }

        file.close();
    }

    void assignMetadata(const std::vector<uint64_t> &metadata, meta_data_t &mtd)
    {
        int index = 0;

        mtd.startaddr = metadata[index++];

        mtd.kernel_id = metadata[index++];

        for (int i = 0; i < 3; i++)
        {
            mtd.kernel_size[i] = metadata[index++];
        }
        m_grid_dim.x = mtd.kernel_size[0];
        m_grid_dim.y = mtd.kernel_size[1];
        m_grid_dim.z = mtd.kernel_size[2];

        mtd.wf_size = metadata[index++];
        mtd.wg_size = metadata[index++];
        mtd.metaDataBaseAddr = metadata[index++];
        mtd.ldsSize = metadata[index++];
        mtd.pdsSize = metadata[index++];
        mtd.sgprUsage = metadata[index++];
        mtd.vgprUsage = metadata[index++];
        mtd.pdsBaseAddr = metadata[index++];

        mtd.num_buffer = metadata[index++] + 1; // add localmem buffer

        mtd.buffer_base = new uint64_t[mtd.num_buffer];

        for (int i = 0; i < mtd.num_buffer - 1; i++)
        {
            mtd.buffer_base[i] = metadata[index++];
            if (mtd.buffer_base[i] == mtd.startaddr)
                mtd.insBufferIndex = i;
        }
        mtd.buffer_base[mtd.num_buffer - 1] = ldsBaseAddr_core; // localmem base addr

        mtd.buffer_size = new uint64_t[mtd.num_buffer];
        for (int i = 0; i < mtd.num_buffer - 1; i++)
        {
            mtd.buffer_size[i] = metadata[index++];
        }
        mtd.buffer_size[mtd.num_buffer - 1] = 0;

        mtd.buffer_allocsize = new uint64_t[mtd.num_buffer];
        for (int i = 0; i < mtd.num_buffer - 1; i++)
        {
            mtd.buffer_allocsize[i] = metadata[index++];
        }
        mtd.buffer_allocsize[mtd.num_buffer - 1] = mtd.ldsSize;
    }

    void readTextFile(const std::string &filename, std::vector<std::vector<uint8_t>> &buffers, meta_data_t mtd)
    {

        std::ifstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return;
        }

        std::string line;
        int bufferIndex = 0;
        std::vector<uint8_t> buffer;
        for (int bufferIndex = 0; bufferIndex < mtd.num_buffer; bufferIndex++)
        {
            buffer.reserve(mtd.buffer_allocsize[bufferIndex]); // 提前分配空间
            int readbytes = 0;
            while (readbytes < mtd.buffer_size[bufferIndex])
            {
                std::getline(file, line);
                for (int i = line.length(); i > 0; i -= 2)
                {
                    std::string hexChars = line.substr(i - 2, 2);
                    uint8_t byte = std::stoi(hexChars, nullptr, 16);
                    buffer.push_back(byte);
                }
                readbytes += 4;
            }
            buffer.resize(mtd.buffer_allocsize[bufferIndex]);
            buffers[bufferIndex] = buffer;
            buffer.clear();
        }

        file.close();
    }

    void init_local_and_private_mem(std::vector<std::vector<uint8_t>> &buffers, meta_data_t mtd)
    {
        uint64_t ldsSize = mtd.ldsSize;
        uint64_t pdsSize = mtd.pdsSize;
        buffers[mtd.num_buffer - 1].resize(ldsSize);
    }

    void init_extmem(std::string datafile)
    {
        m_buffer_data = new std::vector<std::vector<uint8_t>>(m_metadata.num_buffer); // 此时num_buffer已经是.meta文件里的num_buffer+1，包含了末尾的local buffer
        readTextFile(datafile, *m_buffer_data, m_metadata);
        init_local_and_private_mem(*m_buffer_data, m_metadata);
    }

    dim3 m_next_cta = {0, 0, 0}; // start from 0 ~ (grid_dim - 1)
    dim3 m_grid_dim;

public:
    uint64_t start_cycle;
    uint64_t end_cycle;
};

#endif

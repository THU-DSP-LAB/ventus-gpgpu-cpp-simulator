#include "CTA_Scheduler.hpp"

bool CTA_Scheduler::isHexCharacter(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

int CTA_Scheduler::charToHex(char c)
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

void CTA_Scheduler::readHexFile(const std::string &filename, int itemSize, std::vector<uint64_t> &items)
{ // itemSize为每个数据的比特数，这里为64
    ifstream file(filename);

    if (!file)
    {
        cout << "Error opening file: " << filename << endl;
        return;
    }

    char c;
    int bits = 0;
    unsigned long long value = 0;
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
            cout << "Invalid character found: " << c << endl;
            continue;
        }

        int hexValue = charToHex(c);
        if (leftside)
            value = value | (hexValue << bits);
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
        cout << "Warning: Incomplete item found at the end of the file!" << endl;
    }

    file.close();
    std::cout << "CTA: Finish readHexFile()\n";
}

void CTA_Scheduler::assignMetadata(const std::vector<uint64_t> &metadata, meta_data &mtd)
{
    std::cout << "CTA: assignMetadata, metadata size=" << metadata.size() << "\n";

    int index = 0;

    mtd.startaddr = metadata[index++];

    mtd.kernel_id = metadata[index++];

    for (int i = 0; i < 3; i++)
    {
        mtd.kernel_size[i] = metadata[index++];
    }

    mtd.wf_size = metadata[index++];
    mtd.wg_size = metadata[index++];
    mtd.metaDataBaseAddr = metadata[index++];
    mtd.ldsSize = metadata[index++];
    mtd.pdsSize = metadata[index++];
    mtd.sgprUsage = metadata[index++];
    mtd.vgprUsage = metadata[index++];
    mtd.pdsBaseAddr = metadata[index++];

    mtd.num_buffer = metadata[index++] + 1; // add localmem buffer
    cout << "CTA: assign mtd.num_buffer=" << mtd.num_buffer << "(including the extra local memory buffer)\n";

    mtd.buffer_base = new uint64_t[mtd.num_buffer];

    for (int i = 0; i < mtd.num_buffer - 1; i++)
    {
        mtd.buffer_base[i] = metadata[index++];
        if (mtd.buffer_base[i] == mtd.startaddr)
            mtd.insBufferIndex = i;
    }
    mtd.buffer_base[mtd.num_buffer - 1] = 0x70000000; // localmem base addr
    cout << "CTA: mtd.buffer_base=" << std::hex;
    for (int i = 0; i < mtd.num_buffer; i++)
        cout << mtd.buffer_base[i] << ",";
    cout << std::dec << "\n";

    mtd.buffer_size = new uint64_t[mtd.num_buffer];
    for (int i = 0; i < mtd.num_buffer - 1; i++)
    {
        mtd.buffer_size[i] = metadata[index++];
    }
    mtd.buffer_size[mtd.num_buffer - 1] = 0;
    cout << "CTA: mtd.buffer_size=" << std::hex;
    for (int i = 0; i < mtd.num_buffer; i++)
        cout << mtd.buffer_size[i] << ",";
    cout << std::dec << "\n";

    mtd.buffer_allocsize = new uint64_t[mtd.num_buffer];
    for (int i = 0; i < mtd.num_buffer - 1; i++)
    {
        mtd.buffer_allocsize[i] = metadata[index++];
    }
    mtd.buffer_allocsize[mtd.num_buffer - 1] = mtd.ldsSize;
    cout << "CTA: mtd.buffer_allocsize=" << std::hex;
    for (int i = 0; i < mtd.num_buffer; i++)
        cout << mtd.buffer_allocsize[i] << ",";
    cout << std::dec << "\n";

    std::cout << "CTA: Finish assignMetadata()\n";
}

void CTA_Scheduler::freeMetadata(meta_data &mtd)
{
    delete[] mtd.buffer_base;
    delete[] mtd.buffer_size;
}

void CTA_Scheduler::activate_warp()
{

    SC_REPORT_INFO("CTA_Scheduler", "Activating warps...");

    // 处理metadata数据
    uint64_t knum_workgroup = mtd.kernel_size[0] * mtd.kernel_size[1] * mtd.kernel_size[2]; // k means kernel
    cout << "CTA: knum_workgroup=" << knum_workgroup << "\n";
    if (knum_workgroup > 2)
        cout << "CTA warning: currently not support so many workgroups\n";
    int warp_limit = num_warp;
    if (mtd.wg_size > warp_limit)
        cout << "CTA error: wg_size > warp_limit per SM\n";
    for (int i = 0; i < knum_workgroup; i++)
    {
        int warp_counter = 0;
        while (warp_counter < mtd.wg_size)
        {
            // sm_group[i]->WARPS[warp_counter] = new WARP_BONE;
            // sm_group[i]->WARPS[warp_counter]->warp_id = warp_counter;

            sm_group[i]->activate_warp(warp_counter);

            cout << "CTA: SM" << i << " warp" << warp_counter << " is activated\n";
            sm_group[i]->WARPS[warp_counter]->is_warp_activated = true;

            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x300] = 0x00001800;

            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x800] = (mtd.wg_size - warp_counter - 1) * num_thread;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x801] = mtd.wg_size;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x802] = num_thread;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x803] = mtd.metaDataBaseAddr;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x804] = 0;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x805] = (mtd.wg_size - warp_counter - 1);    // warp标号反了
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x806] = 0x70000000;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x807] = 0;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x808] = 0;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x809] = 0;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x810] = 0;
            sm_group[i]->WARPS[warp_counter]->CSR_reg[0x811] = 0;
            ++warp_counter;
        }
        sm_group[i]->mtd = mtd;
        sm_group[i]->mtd.num_buffer = sm_group[i]->mtd.num_buffer;
        sm_group[i]->num_warp_activated = warp_counter;
    }
}

void CTA_Scheduler::CTA_INIT()
{
    CTA_Scheduler::readHexFile(metafilename, 64, metadata);
    CTA_Scheduler::assignMetadata(metadata, mtd);
    CTA_Scheduler::activate_warp();
}

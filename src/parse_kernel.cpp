#include "context_model.hpp"
#include "ventus_cyclesim.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using meta_data_t = ventus_kernel_metadata_t;

// Helpers
bool isHexCharacter(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

int charToHex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return -1; // Invalid character
}

// convert raw metadata buffer into struct meta_data_t
void assignMetadata(const std::vector<uint64_t>& rawdata, meta_data_t& metadata) {
    int index = 0;

    metadata.startaddr = rawdata[index++];

    metadata.kernel_id = rawdata[index++];

    for (int i = 0; i < 3; i++) {
        metadata.kernel_size[i] = rawdata[index++];
    }

    metadata.wf_size = rawdata[index++];
    metadata.wg_size = rawdata[index++];
    metadata.metaDataBaseAddr = rawdata[index++];
    metadata.ldsSize = rawdata[index++];
    metadata.pdsSize = rawdata[index++];
    metadata.sgprUsage = rawdata[index++];
    metadata.vgprUsage = rawdata[index++];
    metadata.pdsBaseAddr = rawdata[index++];

    metadata.num_buffer = rawdata[index++];

    metadata.buffer_base = new uint64_t[metadata.num_buffer];
    for (int i = 0; i < metadata.num_buffer; i++) {
        metadata.buffer_base[i] = rawdata[index++];
    }

    metadata.buffer_size = new uint64_t[metadata.num_buffer];
    for (int i = 0; i < metadata.num_buffer; i++) {
        metadata.buffer_size[i] = rawdata[index++];
    }

    metadata.buffer_allocsize = new uint64_t[metadata.num_buffer];
    for (int i = 0; i < metadata.num_buffer; i++) {
        metadata.buffer_allocsize[i] = rawdata[index++];
    }
}

void readHexFile(const std::string& filename, int itemSize, std::vector<uint64_t>& items) {
    // itemSize为每个数据的比特数，这里为64
    std::ifstream file(filename);

    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
    }

    char c;
    int bits = 0;
    uint64_t value = 0;
    bool leftside = false;

    while (file.get(c)) {
        if (c == '\n') {
            if (bits != 0)
                leftside = true;
            continue;
        }

        if (!isHexCharacter(c)) {
            std::cerr << "Invalid character found: '" << c << "' in " << filename << std::endl;
            continue;
        }

        int hexValue = charToHex(c);
        if (leftside)
            value = value | ((uint64_t)hexValue << (92 - bits));
        else
            value = (value << 4) | hexValue;
        bits += 4;

        if (bits >= itemSize) {
            items.push_back(value);
            value = 0;
            bits = 0;
            leftside = false;
        }
    }

    if (bits > 0) {
        std::cerr << "Warning: Incomplete item found at the end of the file!" << std::endl;
    }

    file.close();
}

std::shared_ptr<ventus_kernel_metadata_t> parse_metadata(const std::filesystem::path& metafile) {
    std::vector<uint64_t> rawdata;
    auto metadata = std::make_shared<ventus_kernel_metadata_t>();
    readHexFile(metafile, 64, rawdata);
    assignMetadata(rawdata, *metadata);
    return metadata;
}

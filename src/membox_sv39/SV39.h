#include <cstdint>
#include <iostream>

/*
    |----++++----++++----++++----++++----++++----++++----++++----++++|
    |6------5555----44-------33-3-----3222-----22-1-----111---------0|
    |3------6543----87-------98-6-----0987-----10-8-----210---------0|
    |<-[38]------------------><- VPN2-><--VPN1-><--VPN0-><--offset-->| VA
    |xxxxxxxx<-------------------PPN2-><--PPN1-><--PPN0-><--offset-->| PA
    |xxxxxxxxxx<-------------------PPN2-><--PPN1-><--PPN0->xxDAGUXWRV| PTE
    valid value for PA:
    0000 0000 0000 0000 <-> 0000 003f ffff ffff 256GiB
    ffff ffc0 0000 0000 <-> ffff ffff ffff ffff 256GiB
*/

class SV39{
public:
    const static uint64_t MaxPhyRange = (1ull << 56ull);
    const static uint64_t PageBits = 12ull;
    const static uint64_t PageSize = 1ull << PageBits; // 4KiB
    const static uint64_t PageElem = 512ull;
    const static uint64_t MegaPageSize = 1ull << 21ull;// 2MiB
    const static uint64_t GigaPageSize = 1ull << 30ull;// 1GiB
    const static uint64_t VALowerCeil = 0x0000004000000000;
    const static uint64_t VAUpperFloor = 0xffffffc000000000;

    const static uint64_t V = 1ull;
    const static uint64_t R = 2ull;
    const static uint64_t W = 4ull;
    const static uint64_t X = 8ull;
    const static uint64_t U = 16ull;
    const static uint64_t G = 32ull;
    const static uint64_t A = 64ull;
    const static uint64_t D = 128ull;

    static uint64_t VAextract(uint64_t VA, uint32_t level){
        if(level == 0)
            return (VA >> 30ull) & 0x1ffull;
        else if(level == 1)
            return (VA >> 21ull) & 0x1ffull;
        else
            return (VA >> 12ull) & 0x1ffull;
    }
    static bool VAcheck(uint64_t VA){
        if(VA >= VALowerCeil && VA < VAUpperFloor) return false;
        else return true;
    }
    static uint64_t PTE2PA(uint64_t PTE){
        return (PTE & 0x003ffffffffffc00ull) << 2ull;
    }
    static uint64_t SetPTE(uint64_t PA, uint64_t mods){
        return ((PA & 0x00fffffffffff000ull) >> 2ull) | mods;
    }

};

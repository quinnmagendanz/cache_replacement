#ifndef __CACHE_H__
#define __CACHE_H__

#include "stats.h"
#include "g_std/g_string.h"
#include "g_std/g_vector.h"
#include "memory_hierarchy.h"

class Cache : public Memory {
    public:
        Cache(g_string name, uint32_t numLines, uint32_t numWays, uint64_t hitLatency, bool isL1Cache, Memory *parent);
        void initStats(AggregateStat* parentStat);

        uint64_t load(Address lineAddr);
        uint64_t store(Address lineAddr);
        void writeback(Address lineAddr) {}
        void invalidate(Address lineAddr);
        uint64_t access(Address lineAddr, bool isWrite);
        void addChild(Memory* child) { children.push_back(child); }

	private:
        g_string name;
        Memory *parent;
        g_vector<Memory*> children;
        uint64_t latency;
        uint32_t numWays;
        bool isL1Cache;
        Counter hits;
        Counter misses;
        Counter invalidations;
        Counter evicts;

        typedef uint64_t tag_t;
        typedef g_vector<tag_t> line_t;
        g_vector<line_t> tagArray;

        // FIXME: Add meta data for you replacement policy
        // (e.g. timestamp for LRU)

		// Maximum replacement metadata = 32bits per entry

		// Used for all cache replacement schemes except ARC
		typedef uint8_t meta_t; // 8 bits per cache line
		typedef g_vector<meta_t> line_meta_t;
		g_vector<line_meta_t> tag_meta;
		
		/*// Used only for ARC replacement
		typedef struct Page {
			uint32_t way; // Only using 4 bits
			tag_t tag; // Only using 14 bits
		} Page; // At most # lines used.
		typedef g_vector<Page> page_list;
		g_vector<uint8_t> PArray;
		g_vector<page_list> T1Array;
		g_vector<page_list> T2Array;
		g_vector<page_list> B1Array; // Does not use way field
		g_vector<page_list> B2Array; // Does not use way field
		// Total max use is (4 + 14 + 14) = 32 bits per entry
		*/

        // FIXME: Implement the following two functions
        void LRUUpdate(uint32_t line, uint32_t way, bool isMiss);
        uint32_t LRU(uint32_t line, tag_t tag);
        void LFUUpdate(uint32_t line, uint32_t way, bool isMiss);
        uint32_t LFU(uint32_t line, tag_t tag);
        void RRUpdate(uint32_t line, uint32_t way, bool isMiss);
        uint32_t RR(uint32_t line, tag_t tag);
        void SRRIPUpdate(uint32_t line, uint32_t way, bool isMiss);
        uint32_t SRRIP(uint32_t line, tag_t tag);
		/*Page findPop(g_vector<Page> vec, tag_t tag, uint32_t way);
		void replace(Page* x, uint32_t p, g_vector<Page> T1, g_vector<Page> T2, g_vector<Page> B1, g_vector<Page> B2);
		void ARCUpdate(uint32_t line, uint32_t way, bool isMiss);
		uint32_t ARC(uint32_t line, tag_t tag);
		*/
};

#endif // __CACHE_H__


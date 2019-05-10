#include <cstdlib>
#include <algorithm>
#include "zsim.h"
#include "log.h"
#include "cache.h"

#define CHOOSE_EVICT_WAY LRU 
#define UPDATE_POLICY LRUUpdate

static bool is_valid(Address a) { return ((a >> 63) & 1); }
static bool is_dirty(Address a) { return ((a >> 62) & 1); }
static uint64_t entry_tag(Address a) { return (a & 0x3fffffffffffffff); }
static Address set_valid(Address a, bool valid) { return (a & 0x7fffffffffffffff) | (((Address) valid) << 63); }
static Address set_dirty(Address a, bool dirty) { return (a & 0xbfffffffffffffff) | (((Address) dirty) << 62); }

Cache::Cache(g_string _name,
             uint32_t _numLines,
             uint32_t _numWays,
             uint64_t _hitLatency,
             bool _isL1Cache,
             Memory *_parent)
            : name(_name), parent(_parent)
            , latency(_hitLatency)
            , numWays(_numWays)
            , isL1Cache(_isL1Cache)
            , tagArray()
			, tag_meta()
			/*, T1Array()
			, T2Array()
			, B1Array()
			, B2Array()*/{
    tagArray.resize(_numLines);
	tag_meta.resize(_numLines);
/*	T1Array.resize(_numLines);
	T2Array.resize(_numLines);
	B1Array.resize(_numLines);
	B2Array.resize(_numLines);*/
    for (auto &tags : tagArray) { tags.resize(_numWays, 0); }
    for (auto &meta : tag_meta) { meta.resize(_numWays, 0); }
}

void Cache::initStats(AggregateStat* parentStat) {
    AggregateStat* cacheStat = new AggregateStat();
    cacheStat->init(name.c_str(), "Cache stats");
    hits.init("hits", "Cache hits");
    misses.init("misses", "Cache misses");
    invalidations.init("invalidations", "Cache invalidations");
    evicts.init("evictions", "Cache evictions");
    cacheStat->append(&hits);
    cacheStat->append(&misses);
    cacheStat->append(&invalidations);
    cacheStat->append(&evicts);
    parentStat->append(cacheStat);
}

uint64_t Cache::load(Address lineAddr) {
    return access(lineAddr, false);
}

uint64_t Cache::store(Address lineAddr) {
    return access(lineAddr, true);
}

// FIXME: Modify Cache::access() function to make it a non-inclusive/exclusive cache
uint64_t Cache::access(Address lineAddr, bool isWrite) {
    Address line = lineAddr % tagArray.size();
    Address tag = lineAddr / tagArray.size();
    uint64_t cycles = latency;
    auto &tags = tagArray[line];
    int way = -1;

    //info("[%s] access %lx %d", name.c_str(), lineAddr, isWrite);

    for (int i = 0; i < (int) tags.size(); ++i) {
        if (is_valid(tags[i]) && entry_tag(tags[i]) == tag) { // hit
            way = i;
            hits.inc();
            break;
        }
    }

    bool isMiss = way == -1;

    if (isMiss) { // miss
        misses.inc();
        way = isL1Cache ? random() % numWays : CHOOSE_EVICT_WAY(line, tag);
        Address victimAddr = (tags[way] * tagArray.size()) + line;
        if (is_dirty(tags[way])) parent->store(victimAddr);
        if (is_valid(tags[way])) {
            for (auto child : children) {
                child->invalidate(victimAddr);
            }
        }
        cycles += parent->load(lineAddr);
        tags[way] = set_valid(tag, true);
        evicts.inc();
    }

    if (isWrite) {
        tags[way] = set_dirty(tags[way], true);
    }
    UPDATE_POLICY(line, way, isMiss);

    return cycles;
}

void Cache::invalidate(Address lineAddr) {
    Address line = lineAddr % tagArray.size();
    Address tag = lineAddr / tagArray.size();
    auto &tags = tagArray[line];
    int way = -1;

    for (int i = 0; i < (int) tags.size(); ++i) {
        if (is_valid(tags[i]) && entry_tag(tags[i]) == tag) { // hit
            way = i;
            break;
        }
    }

    if (way != -1) {
        tags[way] = set_valid(tag, false);
        invalidations.inc();
    }

    for (auto child: children) {
        child->invalidate(lineAddr);
    }

}

// FIXME: Implement your own replacement policy here
void Cache::LRUUpdate(uint32_t line, uint32_t way, bool isMiss) {
    assert(way < numWays);
	auto &line_meta = tag_meta[line];
	for (uint32_t i = 0; i < numWays; i++) {
		if (i != way && line_meta[way] >= line_meta[i]) {
			line_meta[i]++;
		}
	}
	line_meta[way] = 0;
}

uint32_t Cache::LRU(uint32_t line, tag_t tag) {
	auto &line_meta = tag_meta[line];
	uint32_t replaced = 0;
	for (uint32_t i = 0; i < numWays; i++) {
		if (line_meta[i] <= numWays-1) {
			replaced = i;
			break;
		}
	}
	return replaced;
}

void Cache::LFUUpdate(uint32_t line, uint32_t way, bool isMiss) {
    assert(way < numWays);
	auto &line_meta = tag_meta[line];
	if (isMiss) {
		line_meta[way] = 0;
	} else {
		line_meta[way]++;
	}
}

uint32_t Cache::LFU(uint32_t line, tag_t tag) {
	auto &line_meta = tag_meta[line];
	uint32_t replaced = 0;
	for (uint32_t i = 0; i < numWays; i++) {
		if (line_meta[i] < line_meta[replaced]) {
			replaced = i;
		}
	}
	return replaced;
}

void Cache::SRRIPUpdate(uint32_t line, uint32_t way, bool isMiss) {
    assert(way < numWays);
	auto &line_meta = tag_meta[line];
	if (!isMiss) {
		line_meta[way] = 0;
	}
}

uint32_t Cache::SRRIP(uint32_t line, tag_t tag) {
	auto &line_meta = tag_meta[line];
	while (true) {
		for (uint32_t i = 0; i < numWays; i++) {
			if (line_meta[i] == 7) {
				line_meta[i] = 2;
				return i;
			}
		}
		for (uint32_t i = 0; i < numWays; i++) {
			line_meta[i]++;
		}
	}
}


/*void Cache::ARCUpdate(uint32_t line, uint32_t way, bool isMiss) {
    assert(way < numWays);
	auto& T1 = T1Array[line];
	auto& T2 = T2Array[line];
	if (!isMiss) {
		T1.remove(way);
		T2.remove(way);
		T2.pop_front(way);	
	}
}

uint32_t Cache::ARC(uint32_t line, tag_t tag) {
	auto& T1 = T1Array[line];
	auto& T2 = T2Array[line];
	auto& B1 = B1Array[line];
	auto& B2 = B2Array[line];
	uint16_t c_tag = (uint16_t)entry_ctag(tag);
	std::list<int>::iterator t1_1 = std::find (my_list.begin(), my_list.end(), some_value);
	if (
}*/

/*void Cache::RPLRUUpdate(uint32_t line, uint32_t way, bool isMiss) {
    assert(way < numWays);

}

uint32_t Cache::RPLRU(uint32_t line, tag_t tag) {
    return random() % numWays;
}*/

void Cache::RRUpdate(uint32_t line, uint32_t way, bool isMiss) {
    assert(way < numWays);
}

uint32_t Cache::RR(uint32_t line, tag_t tag) {
    return random() % numWays;
}



#include <cstdlib>
#include <algorithm>
#include "g_std/g_vector.h"
#include "zsim.h"
#include "log.h"
#include "cache.h"

#define CHOOSE_EVICT_WAY SRRIP 
#define UPDATE_POLICY SRRIPUpdate

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
			, B2Array()
			, PArray()*/{
    tagArray.resize(_numLines);
	tag_meta.resize(_numLines);
	/*T1Array.resize(_numLines);
	T2Array.resize(_numLines);
	B1Array.resize(_numLines);
	B2Array.resize(_numLines);
	PArray.resize(_numLines);*/
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

/*Cache::Page Cache::findPop(g_vector<Page> vec, tag_t tag, uint32_t way) {
	for (auto it = vec.cbegin(); it != vec.cend(); it++) {
		if ((*it).tag == tag || way == (*it).way) {
			Page found = *it;
			vec.erase(it);
			return found;
		}	
	}
	return (Cache::Page)0;
}

void Cache::replace(Cache::Page* x, uint32_t p, g_vector<Cache::Page> T1, g_vector<Cache::Page> T2, g_vector<Cache::Page> B1, g_vector<Cache::Page> B2) {
	Cache::Page page = (Cache::Page)0;
	if (T1.size() >= 1 && (((page = findPop(B2, x->tag, -1)) != 0 && T1.size() = p) || T1.size() > p)) {
		if (page != 0) B2.insert(B2.begin(), page);
		x->way = T1.back().way;
		B1.insert(B1.begin(), T1.back());
		T1.pop_back();
	} else {
		x->way = T2.back().way;
		B2.insert(B2.begin(), T1.back());
		T2.pop_back();
	}
}

void Cache::ARCUpdate(uint32_t line, uint32_t way, bool isMiss) {
    assert(way < numWays);
	auto& T1 = T1Array[line];
	auto& T2 = T2Array[line];
	if (!isMiss) {
		Cache::Page t1 = findPop(T1, 0, way);
		Cache::Page t2 = findPop(T2, 0, way);
		T2.insert(T2.begin(), (t1 != 0 ? t1 : t2));	
	}
}

uint32_t Cache::ARC(uint32_t line, tag_t tag) {
	auto& p = PArray[line];
	auto& T1 = T1Array[line];
	auto& T2 = T2Array[line];
	auto& B1 = B1Array[line];
	auto& B2 = B2Array[line];
	Cache::Page x;
	x.tag = tag;
	if ((x = findPop(B1, tag, -1)) != 0) {
		p = std::min(numWays, p + std::max(B1.size()/B2.size(), 1));
		replace(&x, p, T1, T2, B1, B2);
		T2.insert(T2.begin(), x);
	} else if ((x = findPop(B2, tag, -1)) != 0) {
		p = std::min(numWays, p + std::max(B1.size()/B2.size(), 1));
		replace(&x, p, T1, T2, B1, B2);
		T2.insert(T2.begin(), x);
	} else if (B1.size() + T1.size() == numWays) {
		if (T1.size() < numWays) {
			B1.pop_back();
			replace(&x, p, T1, T2, B1, B2);
		} else {
			x.way = T1.back().way;
			T1.pop_back();
		}
		T1.insert(T1.begin(), x);
	} else if (B1.size() + T1.size() > numWays && T1.size() + T2.size() + B1.size() + B2.size() >= numWays) {
		if (T1.size() + T2.size() + B1.size() + B2.size() == 2*numWays) {
			B2.pop_back();
		}
		replace(&x, p, T1, T2, B1, B2);
		T1.insert(T1.begin(), x);
	} else {
		assert(false); // Should never get here.
	}
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



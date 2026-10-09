#pragma once

namespace TysAuraCastTiming {

enum TimeSource : unsigned char {
    TIME_SOURCE_NONE = 0,
    TIME_SOURCE_LOCAL_CAST_MODIFIED = 1,
    TIME_SOURCE_LOCAL_COMBO_SCALED = 2,
    TIME_SOURCE_REMOTE_BASE = 3
};

struct Evidence {
    unsigned long durationMs;
    unsigned char source;
    bool valid;
};

struct Stats {
    unsigned long castRequestsSeen;
    unsigned long comboCaptures;
    unsigned long comboConsumes;
    unsigned long localDurations;
    unsigned long localComboDurations;
    unsigned long remoteBaseDurations;
    unsigned long durationMisses;
};

bool initialize();
void onWorldLeaving();

// Called once for each observed SMSG_SPELL_GO. The duration is computed once
// per cast, then copied to every hit target's PendingApplication. Local-player
// combo finishers consume the CMSG_CAST_SPELL snapshot captured at send time.
Evidence resolve(unsigned long long casterGuid, unsigned long spellId,
                 unsigned long now);

const char* sourceName(unsigned char source);
Stats stats();
const char* status();

} // namespace TysAuraCastTiming

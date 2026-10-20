// Read-only career identity helpers; included inside race.cpp's anonymous namespace.
// Evidence and limits: docs/RE-HOST-NEXT.md. Never call the mutating guest resolver 2632F0.
bool NextReadable(uint32_t va, uint32_t bytes) {
    return va >= 0x10000 && bytes && va <= UINT32_MAX - bytes && Mapped(va, bytes);
}

bool NextDbIdentity(int city, int index, uint32_t& descriptor, uint32_t& name) {
    if (city < 0 || city > 3 || index < 0 || index >= 160) return false;
    uint32_t db = P(kRaceDb);
    if (!NextReadable(db, 4) || db > UINT32_MAX - 0x140) return false;
    uint32_t row = db + uint32_t(city) * 0x50;
    if (!NextReadable(row + 0x14, 0x10)) return false;
    uint32_t count = R32(row + 0x14), names = R32(row + 0x18), descriptors = R32(row + 0x20);
    if (count > 160 || uint32_t(index) >= count ||
        !NextReadable(names, 4) || names > UINT32_MAX - uint32_t(index) * 4 ||
        !NextReadable(names + uint32_t(index) * 4, 4) ||
        !NextReadable(descriptors, 4) || descriptors > UINT32_MAX - uint32_t(index) * 0x30) return false;
    descriptor = descriptors + uint32_t(index) * 0x30;
    name = R32(names + uint32_t(index) * 4);
    if (!NextReadable(descriptor, 0x30)) return false;
    // Bound each page probe and require a complete nonempty local filename.
    for (uint32_t i = 0; i < 128; i++) {
        if (name > UINT32_MAX - i || !NextReadable(name + i, 1)) return false;
        if (!G<uint8_t>(name + i)) return i != 0;
    }
    return false;
}

// Open is +A5C..A60, stride4C (27F010). Pending is +A6C..A70,
// stride50 with its race record embedded at +0 (27F0D0). Both use UUID at +4.
bool NextFindRecordList(uint32_t data, uint32_t offset, uint32_t stride, uint16_t id, uint32_t& found) {
    if (data > UINT32_MAX - offset || !NextReadable(data + offset, 12)) return false;
    uint32_t begin = R32(data + offset), end = R32(data + offset + 4), cap = R32(data + offset + 8);
    if (end < begin || cap < end || end - begin > 512 * stride || (end - begin) % stride) return false;
    if (!begin) return !end && !cap;
    if (!NextReadable(begin, 4)) return false;
    for (uint32_t i = 0, count = (end - begin) / stride; i < count; i++) {
        uint32_t record = begin + i * stride;
        if (!NextReadable(record, kRecord) || R32(record) != kRecordVtable) return false;
        if (G<uint16_t>(record + 4) != id) continue;
        if (found) return false; // duplicated local UUID is ambiguous, including across the two lists
        found = record;
    }
    return true;
}

bool ResolveHostLoadedStart(Start& out) {
    uint32_t career = P(kCareer);
    if (!NextReadable(career, 0x10)) return false;
    uint32_t data = R32(career + 0x0C), setup = P(kSetupCurrent);
    if (!NextReadable(data, 0x5A) || !NextReadable(setup, 0x109) || G<uint8_t>(setup + 0x108)) return false;
    uint16_t id = G<uint16_t>(data + 0x58);
    uint32_t record = 0;
    if (!id || !NextFindRecordList(data, 0xA5C, 0x4C, id, record) || !record) return false;
    Start candidate{}; // no transport serial is allocated by a resolver
    candidate.city = G<int8_t>(record + 8);
    candidate.race_idx = G<int16_t>(record + 6);
    uint32_t descriptor = 0, name = 0;
    if (!NextDbIdentity(candidate.city, candidate.race_idx, descriptor, name) ||
        R32(data + 0x50) != uint32_t(candidate.city) || !SetupMatches(setup, candidate.city, candidate.race_idx) ||
        R32(setup + 0x18) != R32(descriptor) || R32(descriptor) == 0 || R32(descriptor) == 1) return false;
    std::memcpy(candidate.record, HostPtr(record), kRecord);
    for (uint32_t i = 0; i + 1 < sizeof candidate.name; i++) {
        candidate.name[i] = char(G<uint8_t>(name + i));
        if (!candidate.name[i]) break;
    }
    if (P(kCareer) != career || P(career + 0x0C) != data || P(kSetupCurrent) != setup ||
        G<uint16_t>(data + 0x58) != id || R32(data + 0x50) != uint32_t(candidate.city)) return false;
    out = candidate;
    return true;
}

bool NativeNextMatches(const Start& start) {
    int16_t host_index = 0;
    std::memcpy(&host_index, start.record + 6, sizeof host_index);
    if (int8_t(start.record[8]) != start.city || host_index != start.race_idx) return false;
    uint32_t descriptor = 0, name = 0;
    if (!NextDbIdentity(start.city, start.race_idx, descriptor, name)) return false;
    uint32_t career = P(kCareer);
    if (!NextReadable(career, 0x10)) return false;
    uint32_t data = R32(career + 0x0C);
    if (!NextReadable(data, 0x14)) return false;
    uint32_t manager = R32(data + 0x10);
    if (!NextReadable(manager, 0x98)) return false;
    uint32_t begin = R32(manager + 0x78), end = R32(manager + 0x7C), active = R32(manager + 0x94);
    if (end < begin || end - begin > 512 * 12 || (end - begin) % 12 || active >= (end - begin) / 12 ||
        !NextReadable(begin, 4) || !NextReadable(begin + active * 12, 12)) return false;
    uint32_t task = R32(begin + active * 12 + 8);
    if (!NextReadable(task, 0xE1) || R32(task) != 0x470348 ||
        !G<uint8_t>(task + 0x0C) || !G<uint8_t>(task + 0xE0)) return false;
    uint32_t count = R32(task + 0x10), entries = R32(task + 0x14), cursor = R32(task + 0xDC);
    if (!count || count > 512 || cursor >= count || !NextReadable(entries, 4) ||
        entries > UINT32_MAX - cursor * 8 || !NextReadable(entries + cursor * 8, 8)) return false;
    uint32_t entry = entries + cursor * 8;
    if (G<uint8_t>(entry + 4)) return false; // movie/named segment, not a race
    uint16_t id = G<uint16_t>(entry + 6); // 2670A0: LOCAL UniqueId, never RaceIdx or host UUID
    uint32_t record = 0;
    if (!id || !NextFindRecordList(data, 0xA5C, 0x4C, id, record) ||
        !NextFindRecordList(data, 0xA6C, 0x50, id, record) || !record ||
        G<int8_t>(record + 8) != start.city || G<int16_t>(record + 6) != start.race_idx) return false;
    return P(kCareer) == career && P(career + 0x0C) == data && P(data + 0x10) == manager &&
           R32(manager + 0x94) == active && R32(manager + 0x78) == begin && R32(manager + 0x7C) == end &&
           R32(begin + active * 12 + 8) == task && R32(task + 0xDC) == cursor &&
           R32(task + 0x14) == entries && R32(task + 0x10) == count &&
           G<uint8_t>(task + 0x0C) && G<uint8_t>(task + 0xE0) &&
           !G<uint8_t>(entry + 4) && G<uint16_t>(entry + 6) == id;
}

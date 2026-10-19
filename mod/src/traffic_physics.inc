// Included by traffic.cpp, inside its anonymous namespace. Data only; never calls guest code.
// Native paths and limits: docs/RE-SYNC-TRAFEGO-FISICA.md.
struct CrashBody { uint32_t collider = 0, body = 0, matrix = 0; };

bool ReadBody(uint32_t c, uint32_t owner, uint32_t inst, uint16_t handle, CrashBody& out) {
    uint32_t b = P(c + 0x10), m = P(c + 0x14);
    // 6FF1F owner, 6FF2D embedded body; 2A3349 inst, 2A3398/33A5 legitimate matrix aliases.
    if (!Valid(c, 0x27C) || P(c + 4) != inst || P(c + 0x78) != owner || b != c + 0x80 ||
        !Valid(b, 0x8C) || !Valid(m, 48) || (m != b + 0x5C && m != inst + 0x10) ||
        G<uint16_t>(c + 8) != handle) return false;
    uint32_t bound = P(c + 0x0C);
    if (!Valid(bound, 0x38) || P(P(inst + 4) + 0x0C) != bound ||
        m != (G<uint8_t>(bound + 7) ? inst + 0x10 : b + 0x5C)) return false;
    float mass = RF(b + 4);
    if (!std::isfinite(mass) || mass <= 0 || mass > 10000000.f) return false;
    for (int i = 0; i < 3; i++) {
        float inertia = RF(b + 0x0C + i * 4);
        if (!std::isfinite(inertia) || inertia <= 0 || inertia > 1e12f) return false;
    }
    out = {c, b, m}; return true;
}
bool ReadCrash(uint32_t s, CrashBody& out) {
    return ReadBody(P(s + 0x13C), s, s + 0x24, G<uint16_t>(s + 0x2C), out);
}
bool ReadTrailerBody(uint32_t s, uint32_t trailer, CrashBody& out) {
    return Valid(trailer, 0x174) && ReadBody(P(trailer + 0xA4), s, trailer + 0x12C,
                                          G<uint16_t>(trailer + 0x134), out);
}

bool MatrixOK(const float* m) {
    for (int i = 0; i < 12; i++) if (!std::isfinite(m[i]) || std::fabs(m[i]) > 1000000.f) return false;
    for (int i = 0; i < 3; i++) {
        float len = 0;
        for (int j = 0; j < 3; j++) len += m[i * 3 + j] * m[i * 3 + j];
        if (len < 0.8f || len > 1.2f) return false;
        for (int k = i + 1; k < 3; k++) {
            float dot = 0;
            for (int j = 0; j < 3; j++) dot += m[i * 3 + j] * m[k * 3 + j];
            if (std::fabs(dot) > 0.15f) return false;
        }
    }
    float determinant = m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]);
    return determinant > 0.75f && determinant < 1.25f;
}

void PoseMatrix(const Car& c, float* m, float ahead) {
    float up[3], forward[3];
    for (int i = 0; i < 3; i++) up[i] = F(c.up[i]), forward[i] = -F(c.fwd[i]);
    // Quantization must not accumulate shear in the body or composite bounds.
    float length = 0; for (float f : forward) length += f*f;
    length = std::sqrt(length);
    for (float& f : forward) f /= length > 1e-6f ? length : 1.f;
    float dot = 0; for (int i = 0; i < 3; i++) dot += up[i]*forward[i];
    length = 0;
    for (int i = 0; i < 3; i++) { up[i] -= dot*forward[i]; length += up[i]*up[i]; }
    length = std::sqrt(length);
    for (float& f : up) f /= length > 1e-6f ? length : 1.f;
    m[0] = up[1]*forward[2]-up[2]*forward[1];
    m[1] = up[2]*forward[0]-up[0]*forward[2];
    m[2] = up[0]*forward[1]-up[1]*forward[0];
    for (int i = 0; i < 3; i++) {
        m[3+i] = up[i]; m[6+i] = forward[i]; m[9+i] = c.pos[i]+c.velocity[i]*ahead;
    }
}

bool Finite(const Car& c) {
    if ((c.flags & ~(kActive|kTrailer|kWreck|kTrailerBody)) || c.reserved[0] || c.reserved[1] ||
        ((c.flags & kTrailerBody) && (!(c.flags & kTrailer) || !(c.flags & kWreck)))) return false;
    if (!(c.flags & kActive)) return c.flags == 0;
    if (c.disabled > 1 || !std::isfinite(c.speed) || std::fabs(c.speed) > 2000.f) return false;
    float flen = 0, ulen = 0, dot = 0;
    for (int i = 0; i < 3; i++) {
        if (!std::isfinite(c.pos[i]) || std::fabs(c.pos[i]) > 1000000.f ||
            !std::isfinite(c.velocity[i]) || std::fabs(c.velocity[i]) > 2000.f ||
            !std::isfinite(c.angular[i]) || std::fabs(c.angular[i]) > 1000.f ||
            !std::isfinite(c.centre[i]) || std::fabs(c.centre[i]) > 1000000.f) return false;
        if ((c.flags & kWreck) && std::fabs(c.centre[i]-c.pos[i]) > 50.f) return false;
        float f = F(c.fwd[i]), u = F(c.up[i]); flen += f*f; ulen += u*u; dot += f*u;
    }
    if (flen < 0.8f || flen > 1.2f || ulen < 0.8f || ulen > 1.2f || std::fabs(dot) > 0.15f) return false;
    if (c.flags & kTrailer) {
        float matrix[12]; std::memcpy(matrix, c.trailer, sizeof matrix);
        if (!MatrixOK(matrix)) return false;
        for (int i = 0; i < 3; i++) if (std::fabs(matrix[9+i]-c.pos[i]) > 100.f) return false;
        for (int i = 0; i < 3; i++) {
            if (!std::isfinite(c.trailer_velocity[i]) || std::fabs(c.trailer_velocity[i]) > 2000.f ||
                !std::isfinite(c.trailer_angular[i]) || std::fabs(c.trailer_angular[i]) > 1000.f ||
                !std::isfinite(c.trailer_centre[i]) || std::fabs(c.trailer_centre[i]) > 1000000.f) return false;
            if ((c.flags & kTrailerBody) && std::fabs(c.trailer_centre[i]-matrix[9+i]) > 50.f) return false;
        }
    }
    return true;
}

bool TrailerTarget(uint32_t s, uint32_t& trailer, uint32_t& matrices, unsigned& count) {
    trailer = P(s + 0xEC);
    uint32_t bound = P(s + 0x28), model = P(bound + 0x0C);
    if (!Valid(trailer, 0x174) || !Valid(model, 0x60) || G<uint8_t>(model + 4) != 0x10) return false;
    // 72B40 accesses every secondary matrix; preflight all of them before taking ownership.
    count = G<uint16_t>(model + 0x4E); matrices = P(model + 0x5C);
    return count >= 2 && count <= 128 && Valid(matrices, count * 48);
}

bool ReadCar(uint32_t s, uint16_t slot, Car& c) {
    c = {}; c.slot = slot;
    if (!Active(s)) return true;
    float visible[12]; std::memcpy(visible, HostPtr(s + 0x34), sizeof visible);
    if (!MatrixOK(visible)) return false;
    c.flags = kActive; c.colour = G<uint8_t>(s + 0x16C);
    c.damage = G<uint8_t>(s + 0x14B); c.disabled = G<uint8_t>(s + 0x14E);
    for (int i = 0; i < 3; i++) {
        c.pos[i] = RF(s + 0x58 + i*4); c.fwd[i] = Q(-RF(s + 0x4C + i*4)); c.up[i] = Q(RF(s + 0x40 + i*4));
    }
    c.speed = RF(s + 0xE0);
    for (int i = 0; i < 3; i++) c.velocity[i] = F(c.fwd[i])*c.speed, c.centre[i] = c.pos[i];
    if (P(s + 0x13C)) {
        CrashBody body;
        if (!ReadCrash(s, body)) return false;
        c.flags |= kWreck;
        std::memcpy(c.velocity, HostPtr(body.body + 0x44), 12);
        std::memcpy(c.angular, HostPtr(body.body + 0x50), 12);
        std::memcpy(c.centre, HostPtr(body.body + 0x80), 12);
    }
    uint32_t t = P(s + 0xEC);
    if (t) {
        if (!Valid(t, 0x174)) return false;
        c.flags |= kTrailer; std::memcpy(c.trailer, HostPtr(t + 0x13C), 48);
        std::memcpy(c.trailer_velocity, c.velocity, 12);
        for (int i = 0; i < 3; i++) c.trailer_centre[i] = c.trailer[9+i];
        if (P(t + 0xA4)) {
            CrashBody body;
            if (!ReadTrailerBody(s, t, body) || !(c.flags & kWreck) ||
                P(t + 4) != P(P(s + 0x13C) + 0x10) || P(t + 8) != body.body) return false;
            c.flags |= kTrailerBody;
            std::memcpy(c.trailer_velocity, HostPtr(body.body + 0x44), 12);
            std::memcpy(c.trailer_angular, HostPtr(body.body + 0x50), 12);
            std::memcpy(c.trailer_centre, HostPtr(body.body + 0x80), 12);
        }
    }
    return Finite(c);
}

void WriteTrailer(uint32_t t, uint32_t matrices, unsigned count, const float* main, const Car& c, float ahead) {
    float trailer[12]; std::memcpy(trailer, c.trailer, sizeof trailer);
    for (int i = 0; i < 3; i++) trailer[9+i] += c.trailer_velocity[i]*ahead;
    std::memcpy(HostPtr(t + 0x13C), trailer, sizeof trailer);
    // Equivalent to 72B40 -> 87B80(inverse main) -> 85930(trailer * inverse main).
    // Updating only t+13C leaves both rendering and composite collision submeshes stale until a native tick.
    float relative[12]{};
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++) relative[i*3+j] += trailer[i*3+k]*main[j*3+k];
    for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++)
        relative[9+j] += (trailer[9+k]-main[9+k])*main[j*3+k];
    for (unsigned i = 1; i < count; i++) std::memcpy(HostPtr(matrices + i*48), relative, sizeof relative);
}

bool BodyCentre(const CrashBody& body, const float* pose, float* centre) {
    uint32_t bound = P(body.collider + 0x0C);
    if (!Valid(bound, 0x38)) return false;
    for (int j = 0; j < 3; j++) {
        centre[j] = pose[9+j];
        if (G<uint8_t>(bound + 7)) for (int i = 0; i < 3; i++) {
            float offset = RF(bound + 0x2C + i*4);
            if (!std::isfinite(offset) || std::fabs(offset) > 50.f) return false;
            centre[j] += offset*pose[i*3+j];
        }
    }
    return true;
}

void WriteBody(uint32_t visual, uint32_t previous, const CrashBody& b, const float* velocity,
               const float* angular, const float* pose, const float* centre) {
    // body+5C may be collider+14 itself: never put the visible origin over the centre of mass.
    std::memcpy(HostPtr(visual), pose, 48);
    if (previous) std::memcpy(HostPtr(previous), pose, 48);
    std::memcpy(HostPtr(b.body + 0x5C), pose, 36);
    std::memcpy(HostPtr(b.body + 0x80), centre, 12);
    // collider+18 caches the previous frame used by 79C85; same visible-origin convention.
    std::memcpy(HostPtr(b.collider + 0x18), pose, 48);
    std::memcpy(HostPtr(b.body + 0x44), velocity, 12);
    std::memcpy(HostPtr(b.body + 0x50), angular, 12);
    float local[3]{};
    for (int j = 0; j < 3; j++) {
        G<float>(b.body + 0x2C + j*4) = RF(b.body + 4)*velocity[j];
        for (int k = 0; k < 3; k++) local[j] += angular[k]*pose[j*3+k];
    }
    for (int j = 0; j < 3; j++) {
        float momentum = 0;
        for (int i = 0; i < 3; i++) momentum += local[i]*RF(b.body + 0x0C + i*4)*pose[i*3+j];
        G<float>(b.body + 0x38 + j*4) = momentum;
    }
}

bool ApplyPhysics(uint32_t s, const Car& c, float ahead) {
    uint32_t trailer = 0, matrices = 0; unsigned count = 0;
    bool articulated = P(s + 0xEC) != 0;
    if (articulated != bool(c.flags & kTrailer)) return false;
    if (articulated && !TrailerTarget(s, trailer, matrices, count)) return false;
    CrashBody body;
    bool physical = P(s + 0x13C) != 0;
    if (physical && !ReadCrash(s, body)) return false;
    CrashBody trailer_body;
    bool trailer_physical = articulated && P(trailer + 0xA4);
    if (trailer_physical && !physical) return false;
    if (trailer_physical && !ReadTrailerBody(s, trailer, trailer_body)) return false;
    if (trailer_physical && (P(trailer + 4) != body.body || P(trailer + 8) != trailer_body.body)) return false;
    // AC=0/A8=0 still takes the road look-ahead branch (76250). Null links are unsafe there.
    // Existing bodies use 79BB0 instead and are relocated by the native active-body path.
    if (!physical && (!Valid(P(s + 0xFC), 0x14) || !Valid(P(s + 0x100), 0x14))) return false;
    float pose[12], centre[3]; PoseMatrix(c, pose, ahead);
    if (!MatrixOK(pose)) return false;
    if (physical) {
        if (c.flags & kWreck) for (int i = 0; i < 3; i++) centre[i] = c.centre[i]+c.velocity[i]*ahead;
        else if (!BodyCentre(body, pose, centre)) return false;
    }
    float trailer_pose[12]{}, trailer_centre[3]{};
    if (articulated) {
        std::memcpy(trailer_pose, c.trailer, sizeof trailer_pose);
        for (int i = 0; i < 3; i++) trailer_pose[9+i] += c.trailer_velocity[i]*ahead;
        if (trailer_physical) {
            if (c.flags & kTrailerBody) for (int i = 0; i < 3; i++)
                trailer_centre[i] = c.trailer_centre[i]+c.trailer_velocity[i]*ahead;
            else if (!BodyCentre(trailer_body, trailer_pose, trailer_centre)) return false;
        }
    }
    float length = RF(P(s + 0x138) + 0x298);
    if (!std::isfinite(length) || length < 0 || length > 100.f) return false;
    // All preflights above this point. Never unlink roads/free lists, create/release bodies, or alter type/variant.
    W32(s + 0xC4, 1); G<uint8_t>(s + 0xAC) = 0;
    G<uint8_t>(s + 0xB1) = 0; G<uint8_t>(s + 0xB2) = 0;
    if (physical) WriteBody(s + 0x34, s + 0x74, body, c.velocity, c.angular, pose, centre);
    else {
        std::memcpy(HostPtr(s + 0x34), pose, 48); std::memcpy(HostPtr(s + 0x74), pose, 48);
    }
    for (int i = 0; i < 3; i++) {
        G<float>(s + 0xD4 + i*4) = -pose[6+i];
        G<float>(s + 0xC8 + i*4) = pose[9+i]-pose[6+i]*length;
    }
    // Disabled/totalled light cars skip UpdateObjectLocation. A nonphysical wreck is a moving proxy until
    // the native engine creates a collider. Keep its update alive, rather than inventing pool ownership.
    G<uint8_t>(s + 0x14B) = physical ? c.damage : std::min<unsigned>(c.damage, 0x7E);
    G<uint8_t>(s + 0x14E) = physical ? c.disabled : 0;
    G<uint8_t>(s + 0x16C) = c.colour;
    if (articulated) {
        WriteTrailer(trailer, matrices, count, pose, c, ahead);
        if (trailer_physical) WriteBody(trailer + 0x13C, 0, trailer_body, c.trailer_velocity,
                                       c.trailer_angular, trailer_pose, trailer_centre);
        g_trailers++;
    }
    // 80D8E: >=0.1 enters 78940. Native 72570 then zeros speed without moving the C8/D4 anchor.
    G<float>(s + 0xE0) = physical ? std::fabs(c.speed) : std::max(std::fabs(c.speed), 0.1f);
    if (physical) g_bodies++;
    else if (c.flags & kWreck) g_proxies++;
    return true;
}

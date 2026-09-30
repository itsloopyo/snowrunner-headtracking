// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/render_discovery.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

#include "hde64.h"

namespace sr_ht::builds {
namespace {

struct Invalid { std::string message; };

void Require(bool condition, const char* message) {
    if (!condition) throw Invalid{message};
}

struct Range {
    std::uint32_t begin, end;
    bool Contains(std::uint32_t address, std::size_t count) const {
        return address >= begin && address < end && count <= end - address;
    }
};

struct Section { Range range; std::uint32_t flags; };
struct Function { Range range; std::uint32_t unwind; };
struct Instruction {
    std::uint32_t address;
    hde64s code;
    int Reg() const { return code.modrm_reg + 8 * code.rex_r; }
    int Rm() const { return code.modrm_rm + 8 * code.rex_b; }
    std::uint32_t End() const { return address + code.len; }
    std::int32_t Displacement() const {
        if (code.flags & F_DISP8) return static_cast<std::int8_t>(code.disp.disp8);
        if (code.flags & F_DISP32) return static_cast<std::int32_t>(code.disp.disp32);
        return 0;
    }
    int Base() const {
        if (code.modrm_mod == 3 || code.p_67) return -1;
        if (code.modrm_rm == 4) {
            if (code.sib_index != 4 || code.rex_x || (code.modrm_mod == 0 && code.sib_base == 5))
                return -1;
            return code.sib_base + 8 * code.rex_b;
        }
        if (code.modrm_mod == 0 && code.modrm_rm == 5) return -1;
        return Rm();
    }
    bool Mov(int dst, int src) const {
        return code.rex_w && code.modrm_mod == 3
            && ((code.opcode == 0x8B && Reg() == dst && Rm() == src)
                || (code.opcode == 0x89 && Rm() == dst && Reg() == src));
    }
    bool Lea(int dst, int base) const {
        return code.opcode == 0x8D && code.rex_w && Reg() == dst && Base() == base;
    }
    bool FloatLoad(int dst, int base) const {
        return code.opcode == 0x0F && code.opcode2 == 0x10 && code.p_rep == 0xF3
            && !code.p_66 && !code.rex_w && Reg() == dst && Base() == base;
    }
    bool Dirty(int base, std::int32_t offset) const {
        return code.opcode == 0x83 && code.modrm_reg == 7 && !code.rex_w
            && !code.p_66 && Base() == base && Displacement() == offset && code.imm.imm8 == 0xFF;
    }
    bool Immediate(int dst, std::uint32_t value) const {
        return !code.rex_w && !code.p_66 && code.opcode >= 0xB8 && code.opcode <= 0xBF
            && code.opcode - 0xB8 + 8 * code.rex_b == dst && code.imm.imm32 == value;
    }
};

class Image {
public:
    Image(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {
        Require(data && size >= 0x40 && size <= std::numeric_limits<std::uint32_t>::max(),
                "image size is invalid");
        Require(Read<std::uint16_t>(0) == 0x5A4D, "image has no MZ signature");
        const auto nt = Read<std::uint32_t>(0x3C);
        Require(nt <= size && size - nt >= 0x108, "PE header is truncated");
        Require(Read<std::uint32_t>(nt) == 0x4550 && Read<std::uint16_t>(nt + 4) == 0x8664,
                "image is not an x64 PE");
        const auto opt = nt + 24;
        Require(Read<std::uint16_t>(opt) == 0x20B && Read<std::uint16_t>(nt + 20) >= 0xF0,
                "image has no PE32+ optional header");
        Require(Read<std::uint32_t>(opt + 56) == size, "mapped image size differs from PE headers");
        const auto count = Read<std::uint16_t>(nt + 6);
        Require(count && count <= 96, "invalid section count");
        const auto optional_size = Read<std::uint16_t>(nt + 20);
        Require(optional_size <= size - opt, "optional header exceeds image");
        const auto table = opt + optional_size;
        Require(table <= size && count * 40u <= size - table, "section table exceeds image");
        for (unsigned i = 0; i < count; ++i) {
            const auto section = table + i * 40;
            const auto begin = Read<std::uint32_t>(section + 12);
            const auto length = Read<std::uint32_t>(section + 8);
            Require(begin < size && length <= size - begin, "section exceeds mapped image");
            if (length) sections_.push_back({{begin, begin + length}, Read<std::uint32_t>(section + 36)});
        }
        std::sort(sections_.begin(), sections_.end(), [](const auto& a, const auto& b) {
            return a.range.begin < b.range.begin;
        });
        for (std::size_t i = 1; i < sections_.size(); ++i)
            Require(sections_[i - 1].range.end <= sections_[i].range.begin, "overlapping sections");
        Require(Read<std::uint32_t>(opt + 108) >= 4, "missing exception directory");
        const auto entries = Read<std::uint32_t>(opt + 112 + 24);
        const auto length = Read<std::uint32_t>(opt + 112 + 28);
        Require(length && length % 12 == 0 && Has(entries, length, 0x40000000),
                "invalid exception directory");
        for (std::uint32_t offset = 0; offset < length; offset += 12) {
            Function f{{Read<std::uint32_t>(entries + offset), Read<std::uint32_t>(entries + offset + 4)},
                       Read<std::uint32_t>(entries + offset + 8)};
            Require(f.range.end > f.range.begin && Has(f.range.begin, f.range.end - f.range.begin, 0x20000000),
                    "unwind function is outside executable sections");
            Require(functions_.empty() || functions_.back().range.end <= f.range.begin,
                    "unordered or overlapping unwind functions");
            functions_.push_back(f);
        }
        for (const auto& f : functions_) roots_.push_back(Primary(f.range.begin));
    }

    template <class T> T Read(std::uint32_t address) const {
        Require(address <= size_ && sizeof(T) <= size_ - address, "read exceeds mapped image");
        T value;
        std::memcpy(&value, data_ + address, sizeof(value));
        return value;
    }

    bool Has(std::uint32_t address, std::size_t count, std::uint32_t flags) const {
        for (const auto& s : sections_)
            if ((s.flags & flags) == flags && s.range.Contains(address, count)) return true;
        return false;
    }

    std::uint32_t Relative(const Instruction& i, std::int32_t displacement) const {
        const auto address = static_cast<std::int64_t>(i.End()) + displacement;
        Require(address >= 0 && static_cast<std::uint64_t>(address) < size_, "relative target exceeds image");
        return static_cast<std::uint32_t>(address);
    }

    std::uint32_t Call(const Instruction& i) const {
        Require(i.code.opcode == 0xE8, "expected a direct call");
        const auto address = Relative(i, static_cast<std::int32_t>(i.code.imm.imm32));
        Require(Has(address, 1, 0x20000000), "call target is not executable");
        return address;
    }

    std::uint32_t Primary(std::uint32_t address) const {
        auto it = std::upper_bound(functions_.begin(), functions_.end(), address,
                                  [](auto a, const auto& f) { return a < f.range.begin; });
        Require(it != functions_.begin(), "address has no unwind function");
        --it;
        if (!it->range.Contains(address, 1))
            throw Invalid{"address has no unwind function: " + std::to_string(address)};
        Function f = *it;
        std::vector<std::uint32_t> seen;
        for (;;) {
            Require(std::find(seen.begin(), seen.end(), f.unwind) == seen.end() && seen.size() < 32,
                    "cyclic or excessive unwind chain");
            seen.push_back(f.unwind);
            Require(Has(f.unwind, 4, 0x40000000), "unreadable unwind header");
            const auto version_flags = Read<std::uint8_t>(f.unwind);
            Require((version_flags & 7) == 1 || (version_flags & 7) == 2, "unsupported unwind version");
            if (!(version_flags & 0x20)) return f.range.begin;
            Require(!(version_flags & 0x18), "chained unwind also declares a handler");
            const auto count = Read<std::uint8_t>(f.unwind + 2);
            const auto codes_size = ((count + 1u) & ~1u) * 2;
            Require(Has(f.unwind, 4 + codes_size + 12, 0x40000000), "truncated unwind chain");
            const auto chain = f.unwind + 4 + codes_size;
            Function next{{Read<std::uint32_t>(chain), Read<std::uint32_t>(chain + 4)},
                          Read<std::uint32_t>(chain + 8)};
            const auto parent = std::lower_bound(functions_.begin(), functions_.end(), next.range.begin,
                                                [](const auto& candidate, auto begin) { return candidate.range.begin < begin; });
            Require(parent != functions_.end() && parent->range.begin == next.range.begin
                    && parent->range.end == next.range.end && parent->unwind == next.unwind,
                    "unwind chain does not identify a function record");
            f = next;
        }
    }

    bool HasFunction(std::uint32_t address) const {
        const auto it = std::upper_bound(functions_.begin(), functions_.end(), address,
                                        [](auto a, const auto& f) { return a < f.range.begin; });
        return it != functions_.begin() && (it - 1)->range.Contains(address, 1);
    }

    Instruction Decode(std::uint32_t address, std::uint32_t end) const {
        Require(address < end && end <= size_, "invalid instruction bounds");
        std::array<std::uint8_t, 16> bytes{};
        std::memcpy(bytes.data(), data_ + address, std::min<std::size_t>(15, end - address));
        Instruction i{address, {}};
        hde64_disasm(bytes.data(), &i.code);
        Require(i.code.len && !(i.code.flags & F_ERROR) && i.code.len <= end - address,
                "invalid or truncated instruction");
        return i;
    }

    std::vector<Instruction> DecodeFunction(std::uint32_t entry) const {
        Require(Primary(entry) == entry, "call does not target a primary function entry");
        std::vector<Instruction> code;
        std::size_t decoded_bytes = 0;
        for (std::size_t index = 0; index < functions_.size(); ++index) {
            const auto& f = functions_[index];
            if (roots_[index] != entry) continue;
            decoded_bytes += f.range.end - f.range.begin;
            Require(decoded_bytes <= 0x100000, "function exceeds decoder limit");
            // A gap must not turn unrelated adjacent vector entries into an instruction sequence.
            if (!code.empty() && code.back().End() != f.range.begin) code.push_back({f.range.begin, {}});
            for (auto address = f.range.begin; address < f.range.end;) {
                auto i = Decode(address, f.range.end);
                code.push_back(i);
                address = i.End();
            }
        }
        Require(!code.empty(), "empty function");
        return code;
    }

    std::uint32_t String(const char* text) const {
        const auto length = std::strlen(text) + 1;
        std::uint32_t found = 0;
        for (const auto& s : sections_) {
            if (!(s.flags & 0x40000000) || s.flags & 0x20000000) continue;
            if (length > s.range.end - s.range.begin) continue;
            for (auto p = s.range.begin; p <= s.range.end - length; ++p) {
                if (p != s.range.begin && data_[p - 1] != 0) continue;
                if (std::memcmp(data_ + p, text, length)) continue;
                Require(!found, "duplicate motion-blur name");
                found = p;
            }
        }
        Require(found != 0, "missing motion-blur name");
        return found;
    }

    std::vector<std::uint32_t> NamedPasses(std::uint32_t plain, std::uint32_t masked) const {
        std::vector<std::uint32_t> candidates;
        for (const auto& s : sections_) {
            if (!(s.flags & 0x20000000)) continue;
            if (s.range.end - s.range.begin < 7) continue;
            for (auto p = s.range.begin; p <= s.range.end - 7; ++p) {
                if ((data_[p] & 0xF8) != 0x48 || data_[p + 1] != 0x8D || (data_[p + 2] & 0xC7) != 5) continue;
                const auto target = static_cast<std::int64_t>(p) + 7 + Read<std::int32_t>(p + 3);
                if (target != plain) continue;
                if (!HasFunction(p)) continue;
                const auto root = Primary(p);
                const auto code = DecodeFunction(root);
                bool have_plain = false, have_masked = false;
                for (const auto& i : code) {
                    if (i.code.opcode != 0x8D || !i.code.rex_w || i.code.modrm_mod || i.code.modrm_rm != 5) continue;
                    const auto reference = Relative(i, i.Displacement());
                    have_plain |= reference == plain;
                    have_masked |= reference == masked;
                }
                if (have_plain && have_masked && std::find(candidates.begin(), candidates.end(), root) == candidates.end())
                    candidates.push_back(root);
            }
        }
        Require(!candidates.empty(), "motion-blur pass is missing");
        return candidates;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::vector<Section> sections_;
    std::vector<Function> functions_;
    std::vector<std::uint32_t> roots_;
};

bool Getter(const Image& image, std::uint32_t entry) {
    const auto code = image.DecodeFunction(entry);
    if (code.size() != 6) return false;
    const auto& first = code[0].code;
    const auto& last = code[4].code;
    if (first.opcode != 0x83 || first.modrm != 0xEC || !first.rex_w || first.imm.imm8 != 0x28
        || last.opcode != 0x83 || last.modrm != 0xC4 || !last.rex_w || last.imm.imm8 != 0x28
        || code[5].code.opcode != 0xC3 || code[1].code.opcode != 0xE8) return false;
    for (int index : {2, 3}) {
        const auto& i = code[index];
        if (i.code.opcode != 0x8B || !i.code.rex_w || i.Reg() != 0 || i.Base() != 0
            || i.Displacement() < 0 || i.Displacement() > 0x1000 || i.Displacement() % 8) return false;
    }
    const auto global_getter = image.Call(code[1]);
    if (!image.Has(global_getter, 8, 0x60000000)) return false;
    const auto load = image.Decode(global_getter, global_getter + 8);
    if (load.code.opcode != 0x8D || !load.code.rex_w || load.Reg() != 0
        || load.code.modrm_mod != 0 || load.code.modrm_rm != 5 || load.code.len != 7
        || image.Read<std::uint8_t>(load.End()) != 0xC3) return false;
    const auto global = image.Relative(load, load.Displacement());
    return global % 8 == 0 && image.Has(global, 8, 0xC0000000);
}

bool WritesRegister(const Instruction& i, int reg) {
    const auto& h = i.code;
    if (!h.len) return false;
    if (h.opcode == 0xE8 || h.opcode == 0xE9 || h.opcode == 0xEB || h.opcode == 0x90
        || (h.opcode >= 0x70 && h.opcode <= 0x7F) || (h.opcode >= 0x50 && h.opcode <= 0x57)) return false;
    if (h.opcode >= 0x58 && h.opcode <= 0x5F) return h.opcode - 0x58 + 8 * h.rex_b == reg;
    if (h.opcode >= 0xB8 && h.opcode <= 0xBF) return h.opcode - 0xB8 + 8 * h.rex_b == reg;
    if (h.opcode == 0x0F) {
        if (h.opcode2 >= 0x80 && h.opcode2 <= 0x8F) return false;
        switch (h.opcode2) {
        case 0x10: case 0x11: case 0x28: case 0x29: case 0x2E: case 0x2F:
        case 0x54: case 0x55: case 0x56: case 0x57: case 0x58: case 0x59:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F: case 0xC6:
            return false;
        default:
            throw Invalid{"unsupported register-flow instruction"};
        }
    }
    switch (h.opcode) {
    case 0x8B: case 0x8D: case 0x03: case 0x0B: case 0x23: case 0x2B: case 0x33:
        return i.Reg() == reg;
    case 0x89: case 0x01: case 0x09: case 0x21: case 0x29: case 0x31: case 0xC7:
        return h.modrm_mod == 3 && i.Rm() == reg;
    case 0x81: case 0x83:
        return h.modrm_reg != 7 && h.modrm_mod == 3 && i.Rm() == reg;
    case 0x39: case 0x3B: case 0x85:
        return false;
    default:
        throw Invalid{"unsupported register-flow instruction"};
    }
}

void ValidateRays(const Image& image, const std::vector<Instruction>& code, MotionBlurDiscovery& result) {
    std::uint32_t fov = 0, aspect = 0, inverse = 0, inverse_offset = 0;
    int fov_count = 0, aspect_count = 0, inverse_count = 0, loop_count = 0;
    std::size_t saves_camera = code.size(), saves_output = code.size();
    for (std::size_t n = 0; n < code.size(); ++n) {
        if (code[n].Mov(7, 1)) { Require(saves_camera == code.size(), "camera argument saved twice"); saves_camera = n; }
        if (code[n].Mov(6, 2)) { Require(saves_output == code.size(), "output argument saved twice"); saves_output = n; }
    }
    Require(saves_camera < code.size() && saves_output < code.size(), "ray-builder argument ownership differs");
    for (std::size_t n = 0; n < code.size(); ++n) {
        const auto& i = code[n];
        if (i.FloatLoad(0, 1)) {
            Require(n + 4 < code.size() && code[n + 1].Mov(6, 2) && code[n + 3].Mov(7, 1)
                    && code[n + 4].code.opcode == 0xE8, "FOV value does not reach the ray-angle helper");
            const auto& half = code[n + 2];
            Require(half.code.opcode == 0x0F && half.code.opcode2 == 0x59 && half.code.p_rep == 0xF3
                    && half.Reg() == 0 && half.code.modrm_mod == 0 && half.code.modrm_rm == 5,
                    "ray angle is not scaled by a scalar constant");
            const auto constant = image.Relative(half, half.Displacement());
            Require(image.Has(constant, 4, 0x40000000) && image.Read<float>(constant) == 0.5f,
                    "ray angle is not a half angle");
            image.Call(code[n + 4]);
            fov = static_cast<std::uint32_t>(i.Displacement());
            ++fov_count;
        }
        if (i.code.opcode == 0x0F && i.code.opcode2 == 0x59 && i.code.p_rep == 0xF3
            && i.Base() == 7 && i.Reg() == 4) {
            aspect = static_cast<std::uint32_t>(i.Displacement()); ++aspect_count;
        }
        if (n + 6 < code.size() && i.Dirty(7, i.Displacement())
            && code[n + 1].code.opcode == 0x75 && code[n + 2].Mov(8, 7)
            && code[n + 3].Lea(1, 7) && code[n + 3].Displacement() == i.Displacement()
            && code[n + 4].code.opcode == 0x33 && code[n + 4].code.modrm == 0xD2
            && !code[n + 4].code.rex_w && code[n + 5].code.opcode == 0xE8
            && code[n + 6].Lea(8, 7) && code[n + 6].Displacement() == i.Displacement()) {
            Require(image.Relative(code[n + 1], static_cast<std::int8_t>(code[n + 1].code.imm.imm8)) == code[n + 6].address,
                    "inverse-cache branch bypasses the wrong region");
            inverse = image.Call(code[n + 5]);
            Require(image.Primary(inverse) == inverse, "inverse target is not a function entry");
            inverse_offset = static_cast<std::uint32_t>(i.Displacement());
            ++inverse_count;
        }
        if (i.Immediate(15, 4)) ++loop_count;
    }
    Require(fov_count == 1 && aspect_count == 1 && inverse_count == 1 && loop_count == 1,
            "ray-builder layout or inverse relationship is missing or ambiguous");
    Require(fov % 4 == 0 && fov >= 64 && fov < 0x1000 && aspect == fov + 4
            && inverse_offset % 16 == 0 && inverse_offset >= 64 && inverse_offset + 64 <= fov,
            "ray-builder fields have invalid widths, alignment or bounds");
    bool output_loop = false;
    for (std::size_t n = 0; n + 4 < code.size(); ++n) {
        const auto& i = code[n];
        if (i.code.opcode != 0x0F || i.code.opcode2 != 0x11 || i.code.p_rep || i.code.p_66
            || i.Base() != 6 || i.Displacement() != 0) continue;
        const auto& stride = code[n + 1].code;
        const auto& decrement = code[n + 3].code;
        if (stride.opcode != 0x83 || stride.modrm != 0xC6 || !stride.rex_w || stride.imm.imm8 != 16
            || decrement.opcode != 0x83 || decrement.modrm != 0xEF || !decrement.rex_w
            || !decrement.rex_b || decrement.imm.imm8 != 1) continue;
        const auto& branch = code[n + 4];
        if (branch.code.opcode != 0x75 && !(branch.code.opcode == 0x0F && branch.code.opcode2 == 0x85)) continue;
        const auto loop = image.Relative(branch, branch.code.opcode == 0x75
                                        ? static_cast<std::int8_t>(branch.code.imm.imm8)
                                        : static_cast<std::int32_t>(branch.code.imm.imm32));
        const auto found = std::find_if(code.begin(), code.end(), [loop](const auto& instruction) {
            return instruction.address == loop;
        });
        Require(found != code.end() && found->Dirty(7, static_cast<std::int32_t>(inverse_offset)),
                "ray output loop does not return to the validated cache access");
        Require(!output_loop, "duplicate ray output loop");
        Require(saves_output < n && saves_camera < n, "ray output precedes argument capture");
        for (std::size_t k = saves_output + 1; k < n; ++k)
            Require(!WritesRegister(code[k], 6), "ray output pointer overwritten");
        for (std::size_t k = saves_camera + 1; k < n; ++k)
            Require(!WritesRegister(code[k], 7), "ray camera pointer overwritten");
        output_loop = true;
    }
    Require(output_loop, "ray-builder output is not four 16-byte records");
    result.inverse = inverse;
    result.inverse_view = inverse_offset;
    result.vertical_fov = fov;
    result.aspect = aspect;
}

MotionBlurDiscovery Resolve(const Image& image) {
    const auto passes = image.NamedPasses(image.String("MotionBlur"), image.String("MotionBlurMSStencilMasked"));
    MotionBlurDiscovery result;
    unsigned matches = 0;
    for (const auto pass : passes) {
        const auto code = image.DecodeFunction(pass);
        for (std::size_t n = 2; n + 4 < code.size(); ++n) {
            if (code[n].code.opcode != 0xE8 || !code[n - 1].Mov(1, 6)
                || !code[n - 2].Lea(2, 5) || !code[n + 2].Immediate(9, 4)
                || !code[n + 3].Lea(8, 5)
                || code[n + 3].Displacement() != code[n - 2].Displacement()) continue;
            const auto rays = image.Call(code[n]);
            const auto ray_code = image.DecodeFunction(rays);
            MotionBlurDiscovery candidate;
            ValidateRays(image, ray_code, candidate);
            unsigned getters = 0;
            for (std::size_t g = 0; g + 1 < n; ++g) {
                if (code[g].code.opcode != 0xE8 || !code[g + 1].Mov(6, 0)) continue;
                const auto getter = image.Call(code[g]);
                if (!Getter(image, getter)) continue;
                // RSI is nonvolatile across calls. Its identity must survive every
                // path from the getter, and no branch may enter after that capture.
                for (std::size_t k = g + 2; k < n; ++k) {
                    Require(code[k - 1].End() == code[k].address && code[k].code.len,
                            "camera flow crosses a noncontiguous function fragment");
                    Require(!WritesRegister(code[k], 6), "camera identity overwritten before ray call");
                }
                for (const auto& i : code) {
                    const bool short_branch = i.code.opcode == 0xEB || (i.code.opcode >= 0x70 && i.code.opcode <= 0x7F);
                    const bool long_branch = i.code.opcode == 0xE9
                        || (i.code.opcode == 0x0F && i.code.opcode2 >= 0x80 && i.code.opcode2 <= 0x8F);
                    if (!short_branch && !long_branch) continue;
                    const auto target = image.Relative(i, short_branch ? static_cast<std::int8_t>(i.code.imm.imm8)
                                                                      : static_cast<std::int32_t>(i.code.imm.imm32));
                    if (target > code[g].address && target <= code[n].address)
                        Require(i.address >= code[g].address && i.address < code[n].address,
                                "branch bypasses the player-camera getter");
                }
                candidate.player_camera_getter = getter;
                ++getters;
            }
            Require(getters == 1, "ray camera does not have a unique validated getter");
            candidate.view_rays = rays;
            candidate.motion_blur_return = code[n].End();
            result = candidate;
            ++matches;
        }
    }
    Require(matches == 1, "motion-blur ray call is missing or ambiguous");
    return result;
}

}  // namespace

bool DiscoverMotionBlur(const std::uint8_t* image, std::size_t size,
                        MotionBlurDiscovery& result, std::string& error) {
    result = {};
    error.clear();
    try {
        const Image mapped(image, size);
        result = Resolve(mapped);
        return true;
    } catch (const Invalid& failure) {
        error = failure.message;
        return false;
    }
}

}  // namespace sr_ht::builds

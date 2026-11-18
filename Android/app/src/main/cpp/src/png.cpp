// PocketDoomBuilder — Android port (Dear ImGui)
// png.cpp — Minimal PNG decoder with a built-in DEFLATE implementation.
//
// zlib is not guaranteed to be available to the NDK build (and is not on
// the host used for CI), so the inflate algorithm lives here. It follows
// the reference "puff" implementation structure (fixed + dynamic Huffman
// blocks, stored blocks) and is intentionally compact rather than fast:
// toolbar icons are small and decoded once at startup.

#include "png.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace pdb {

namespace {

// ---------------------------------------------------------------------------
// DEFLATE
// ---------------------------------------------------------------------------

struct Huffman {
    // Canonical Huffman decoding: count[] per bit length, symbol[] sorted.
    std::vector<int> count;  // size 16
    std::vector<int> symbol; // up to 288
};

struct BitReader {
    const uint8_t* data = nullptr;
    size_t sizeBits = 0;
    size_t bitPos = 0;
    bool error = false;

    void init(const uint8_t* d, size_t n) {
        data = d;
        sizeBits = n * 8u;
        bitPos = 0;
        error = false;
    }

    // Reads `n` bits (1..24) LSB-first, as DEFLATE requires.
    int bits(int n) {
        if (n == 0) return 0;
        if (bitPos + (size_t)n > sizeBits) {
            error = true;
            return 0;
        }
        int value = 0;
        for (int i = 0; i < n; ++i) {
            const size_t abs = bitPos + (size_t)i;
            const int bit = (data[abs >> 3] >> (abs & 7)) & 1;
            value |= bit << i;
        }
        bitPos += (size_t)n;
        return value;
    }

    void alignToByte() { bitPos = (bitPos + 7u) & ~(size_t)7u; }

    size_t bytePos() const { return bitPos >> 3; }
    void setBytePos(size_t p) { bitPos = p * 8u; }
};

// Build a canonical Huffman table from code lengths (puff-style).
void buildHuffman(Huffman& h, const int* lengths, int n) {
    h.count.assign(16, 0);
    h.symbol.assign((size_t)n, 0);
    for (int i = 0; i < n; ++i) h.count[(size_t)lengths[i]]++;
    h.count[0] = 0;
    std::vector<int> offs(16, 0);
    for (int i = 1; i < 16; ++i) offs[(size_t)i] = offs[(size_t)i - 1] + h.count[(size_t)i - 1];
    for (int i = 0; i < n; ++i) {
        if (lengths[i] != 0) h.symbol[(size_t)offs[(size_t)lengths[i]]++] = i;
    }
}

int decodeSymbol(BitReader& br, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= br.bits(1);
        if (br.error) return -1;
        const int count = h.count[(size_t)len];
        if (code - first < count) {
            return h.symbol[(size_t)(index + (code - first))];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

const int kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                             15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                             67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,
                           17,   25,   33,   49,   65,   97,    129,   193,
                           257,  385,  513,  769,  1025, 1537,  2049,  3073,
                           4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateCodes(BitReader& br, std::vector<uint8_t>& out,
                  const Huffman& lengthCode, const Huffman& distCode) {
    // Corrupt input cannot spin forever: a well-formed block cannot produce
    // more symbols than the stream has bits (every symbol costs at least one
    // bit), so bound the iteration count by the bits available.
    size_t budget = br.sizeBits - br.bitPos + 1u;
    for (;;) {
        if (budget-- == 0 || br.error) return false;
        int symbol = decodeSymbol(br, lengthCode);
        if (symbol < 0) return false;
        if (symbol < 256) {
            out.push_back((uint8_t)symbol);
        } else if (symbol == 256) {
            return true; // end of block
        } else {
            symbol -= 257;
            if (symbol >= 29) return false;
            const int length = kLengthBase[symbol] + br.bits(kLengthExtra[symbol]);
            if (br.error) return false;
            int dsym = decodeSymbol(br, distCode);
            if (dsym < 0 || dsym >= 30) return false;
            const int dist = kDistBase[dsym] + br.bits(kDistExtra[dsym]);
            if (br.error || (size_t)dist > out.size()) return false;
            const size_t start = out.size() - (size_t)dist;
            for (int i = 0; i < length; ++i) out.push_back(out[start + (size_t)i]);
        }
    }
}

// Returns false on error; sets `lastBlock`.
bool inflateBlock(BitReader& br, std::vector<uint8_t>& out, bool& lastBlock,
                  std::string& error) {
    lastBlock = br.bits(1) != 0;
    const int type = br.bits(2);
    if (br.error) {
        error = "Truncated DEFLATE stream.";
        return false;
    }

    if (type == 0) {
        // Stored block: skip to the byte boundary and copy.
        br.alignToByte();
        const size_t pos = br.bytePos();
        const size_t dataLen = br.sizeBits / 8;
        if (pos + 4 > dataLen) {
            error = "Truncated stored block.";
            return false;
        }
        const int len = br.data[pos] | (br.data[pos + 1] << 8);
        const int nlen = br.data[pos + 2] | (br.data[pos + 3] << 8);
        if ((len ^ 0xFFFF) != nlen) {
            error = "Corrupt stored block length.";
            return false;
        }
        if (pos + 4 + (size_t)len > dataLen) {
            error = "Truncated stored block data.";
            return false;
        }
        out.insert(out.end(), br.data + pos + 4, br.data + pos + 4 + (size_t)len);
        br.setBytePos(pos + 4 + (size_t)len);
        return true;
    }

    if (type == 1) {
        // Fixed Huffman codes.
        int lengths[288];
        int i = 0;
        for (; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < 288; ++i) lengths[i] = 8;
        Huffman lenCode, distCode;
        buildHuffman(lenCode, lengths, 288);
        for (int k = 0; k < 30; ++k) lengths[k] = 5;
        buildHuffman(distCode, lengths, 30);
        return inflateCodes(br, out, lenCode, distCode);
    }

    if (type == 2) {
        // Dynamic Huffman codes.
        const int nlen = br.bits(5) + 257;
        const int ndist = br.bits(5) + 1;
        const int ncode = br.bits(4) + 4;
        if (br.error || nlen > 286 || ndist > 30) {
            error = "Corrupt dynamic Huffman header.";
            return false;
        }
        int order[19];
        static const int kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                       11, 4,  12, 3, 13, 2, 14, 1, 15};
        for (int i = 0; i < 19; ++i) order[i] = 0;
        for (int i = 0; i < ncode; ++i) order[kOrder[i]] = br.bits(3);
        if (br.error) {
            error = "Truncated code-length table.";
            return false;
        }
        Huffman codeLenCode;
        buildHuffman(codeLenCode, order, 19);

        std::vector<int> lengths((size_t)(nlen + ndist), 0);
        int index = 0;
        while (index < nlen + ndist) {
            const int symbol = decodeSymbol(br, codeLenCode);
            if (symbol < 0) {
                error = "Corrupt code-length symbol.";
                return false;
            }
            if (symbol < 16) {
                lengths[(size_t)index++] = symbol;
            } else {
                int repeat = 0, value = 0;
                if (symbol == 16) {
                    if (index == 0) {
                        error = "Repeat with no previous length.";
                        return false;
                    }
                    value = lengths[(size_t)(index - 1)];
                    repeat = 3 + br.bits(2);
                } else if (symbol == 17) {
                    repeat = 3 + br.bits(3);
                } else {
                    repeat = 11 + br.bits(7);
                }
                if (br.error || index + repeat > nlen + ndist) {
                    error = "Corrupt code-length repeat.";
                    return false;
                }
                while (repeat-- > 0) lengths[(size_t)index++] = value;
            }
        }

        Huffman lenCode, distCode;
        buildHuffman(lenCode, lengths.data(), nlen);
        buildHuffman(distCode, lengths.data() + nlen, ndist);
        return inflateCodes(br, out, lenCode, distCode);
    }

    error = "Invalid DEFLATE block type.";
    return false;
}

// ---------------------------------------------------------------------------
// PNG chunk helpers
// ---------------------------------------------------------------------------

uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool chunkType(const uint8_t* p, const char* type) {
    return std::memcmp(p, type, 4) == 0;
}

int paethPredictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

} // namespace

bool inflateRaw(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                std::string& error) {
    out.clear();
    BitReader br;
    br.init(data, size);
    // A zlib stream of N bytes has at most ~8N blocks (an empty stored block
    // is 5 bytes); anything beyond that is a malformed stream, not data.
    size_t blocks = size * 2u + 16u;
    for (;;) {
        if (blocks-- == 0) {
            error = "Too many DEFLATE blocks (corrupt stream).";
            return false;
        }
        bool last = false;
        if (!inflateBlock(br, out, last, error)) return false;
        if (last) break;
        if (br.error) {
            error = "Truncated DEFLATE stream.";
            return false;
        }
    }
    return true;
}

bool decodePng(const std::vector<uint8_t>& bytes, ImageRGBA& out,
               std::string& error) {
    out = ImageRGBA();
    error.clear();

    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, 8) != 0) {
        error = "Not a PNG file.";
        return false;
    }

    size_t pos = 8;
    uint32_t width = 0, height = 0;
    int bitDepth = 0, colorType = 0, interlace = 0;
    std::vector<uint8_t> idat;
    std::vector<uint8_t> palette; // RGB triples
    std::vector<uint8_t> paletteAlpha;
    bool haveHeader = false, haveEnd = false;

    while (pos + 12 <= bytes.size()) {
        const uint32_t len = be32(bytes.data() + pos);
        const uint8_t* type = bytes.data() + pos + 4;
        const uint8_t* data = bytes.data() + pos + 8;
        if (pos + 12ull + len > bytes.size()) {
            error = "Truncated PNG chunk.";
            return false;
        }
        if (chunkType(type, "IHDR")) {
            if (len < 13) {
                error = "Short IHDR chunk.";
                return false;
            }
            width = be32(data);
            height = be32(data + 4);
            bitDepth = data[8];
            colorType = data[9];
            interlace = data[12];
            haveHeader = true;
        } else if (chunkType(type, "PLTE")) {
            palette.assign(data, data + len);
        } else if (chunkType(type, "tRNS")) {
            paletteAlpha.assign(data, data + len);
        } else if (chunkType(type, "IDAT")) {
            idat.insert(idat.end(), data, data + len);
        } else if (chunkType(type, "IEND")) {
            haveEnd = true;
            break;
        }
        pos += 12ull + len;
    }

    if (!haveHeader) {
        error = "PNG has no IHDR chunk.";
        return false;
    }
    if (!haveEnd && idat.empty()) {
        error = "PNG has no image data.";
        return false;
    }
    if (bitDepth != 8) {
        error = "Only 8-bit PNGs are supported.";
        return false;
    }
    int channels = 0;
    switch (colorType) {
        case 0: channels = 1; break; // greyscale
        case 2: channels = 3; break; // RGB
        case 3: channels = 1; break; // palette
        case 4: channels = 2; break; // grey + alpha
        case 6: channels = 4; break; // RGBA
        default:
            error = "Unsupported PNG colour type.";
            return false;
    }
    if (colorType == 3 && palette.empty()) {
        error = "Palette PNG without a PLTE chunk.";
        return false;
    }
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        error = "Unreasonable PNG dimensions.";
        return false;
    }

    // IDAT holds a zlib stream: 2-byte header (CMF/FLG), DEFLATE data,
    // then an Adler-32 trailer. Skip the header and hand the DEFLATE
    // payload to our raw inflater.
    if (idat.size() < 6) {
        error = "PNG image data is too short.";
        return false;
    }
    if ((idat[0] & 0x0F) != 8) {
        error = "Unsupported zlib compression method in PNG.";
        return false;
    }
    std::vector<uint8_t> raw;
    if (!inflateRaw(idat.data() + 2, idat.size() - 2, raw, error)) return false;

    // Scanlines to decode: either the whole image, or the Adam7 passes.
    struct Pass { uint32_t w, h, startX, startY, stepX, stepY; };
    static const Pass kAdam7[7] = {
        {0, 0, 0, 0, 8, 8}, {0, 0, 4, 0, 8, 8}, {0, 0, 0, 4, 4, 8},
        {0, 0, 2, 0, 4, 4}, {0, 0, 0, 2, 2, 4}, {0, 0, 1, 0, 2, 2},
        {0, 0, 0, 1, 1, 2},
    };
    std::vector<Pass> passes;
    if (interlace == 0) {
        passes.push_back(Pass{width, height, 0, 0, 1, 1});
    } else if (interlace == 1) {
        for (const Pass& base : kAdam7) {
            Pass p = base;
            p.w = width > p.startX ? (width - p.startX + p.stepX - 1) / p.stepX : 0;
            p.h = height > p.startY ? (height - p.startY + p.stepY - 1) / p.stepY : 0;
            if (p.w > 0 && p.h > 0) passes.push_back(p);
        }
    } else {
        error = "Unsupported PNG interlace method.";
        return false;
    }

    // Output buffer in RGBA; the per-pass filters are undone into a
    // temporary scanline buffer and then scattered into place.
    out.width = (int)width;
    out.height = (int)height;
    out.pixels.assign((size_t)width * (size_t)height * 4u, 0);

    size_t rawOfs = 0;
    for (const Pass& p : passes) {
        const size_t pStride = (size_t)p.w * (size_t)channels;
        std::vector<uint8_t> current(pStride, 0);
        std::vector<uint8_t> previous(pStride, 0);
        for (uint32_t y = 0; y < p.h; ++y) {
            if (rawOfs + 1 + pStride > raw.size()) {
                error = "PNG image data is shorter than expected.";
                return false;
            }
            const uint8_t filter = raw[rawOfs++];
            const uint8_t* src = raw.data() + rawOfs;
            rawOfs += pStride;
            for (size_t x = 0; x < pStride; ++x) {
                const int a = x >= (size_t)channels ? current[x - channels] : 0;
                const int b = previous[x];
                const int c = x >= (size_t)channels ? previous[x - channels] : 0;
                int value = src[x];
                switch (filter) {
                    case 0: break;
                    case 1: value += a; break;
                    case 2: value += b; break;
                    case 3: value += (a + b) / 2; break;
                    case 4: value += paethPredictor(a, b, c); break;
                    default:
                        error = "Unknown PNG filter type.";
                        return false;
                }
                current[x] = (uint8_t)(value & 0xFF);
            }

            // Scatter this scanline into the RGBA output.
            const uint32_t outY = p.startY + y * p.stepY;
            for (uint32_t i = 0; i < p.w; ++i) {
                const uint32_t outX = p.startX + i * p.stepX;
                const uint8_t* s = current.data() + (size_t)i * channels;
                uint8_t* d = out.pixels.data() + ((size_t)outY * width + outX) * 4u;
                switch (colorType) {
                    case 0:
                        d[0] = d[1] = d[2] = s[0];
                        d[3] = 255;
                        break;
                    case 2:
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                        d[3] = 255;
                        break;
                    case 3: {
                        const size_t idx = s[0];
                        if (idx * 3 + 2 < palette.size()) {
                            d[0] = palette[idx * 3];
                            d[1] = palette[idx * 3 + 1];
                            d[2] = palette[idx * 3 + 2];
                        }
                        d[3] = idx < paletteAlpha.size() ? paletteAlpha[idx] : 255;
                        break;
                    }
                    case 4:
                        d[0] = d[1] = d[2] = s[0];
                        d[3] = s[1];
                        break;
                    case 6:
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                        d[3] = s[3];
                        break;
                }
            }
            previous.swap(current);
            std::fill(current.begin(), current.end(), (uint8_t)0);
        }
    }

    return true;
}

} // namespace pdb

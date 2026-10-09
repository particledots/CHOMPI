#include "WavetableImport.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace wtimport
{
namespace
{
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
} // namespace

bool readWav(const uint8_t* b, size_t n, std::vector<float>& mono, int& sampleRate, int& clmFrameSize, std::string& error)
{
    mono.clear();
    clmFrameSize = 0;
    if (b == nullptr || n < 12 || std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WAVE", 4) != 0)
    {
        error = "not a WAV file";
        return false;
    }
    int code = 0, ch = 0, bits = 0;
    sampleRate = 0;
    size_t pos = 12;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    while (pos + 8 <= n)
    {
        const uint32_t sz = rd32(b + pos + 4);
        const size_t body = pos + 8;
        const size_t avail = std::min<size_t>(sz, n - body); // a truncated last chunk is read as far as it goes
        if (std::memcmp(b + pos, "fmt ", 4) == 0 && avail >= 16)
        {
            code = rd16(b + body);
            ch = rd16(b + body + 2);
            sampleRate = (int) rd32(b + body + 4);
            bits = rd16(b + body + 14);
            if (code == 0xFFFE && avail >= 26) code = rd16(b + body + 24); // WAVE_FORMAT_EXTENSIBLE: the sub-format
        }
        else if (std::memcmp(b + pos, "clm ", 4) == 0)
        {
            // Serum-style tables carry text such as "<!>2048 10000000 wavetable ..." in this chunk
            for (size_t i = 0; i + 3 < avail; ++i)
                if (std::memcmp(b + body + i, "<!>", 3) == 0)
                {
                    long v = 0;
                    size_t j = i + 3;
                    while (j < avail && b[body + j] >= '0' && b[body + j] <= '9' && v < 1000000) v = v * 10 + (b[body + j++] - '0');
                    if (v >= 64 && v <= 16384) clmFrameSize = (int) v;
                    break;
                }
        }
        else if (std::memcmp(b + pos, "data", 4) == 0)
        {
            data = b + body;
            dataLen = avail;
            break;
        }
        pos = body + (size_t) sz + (sz & 1);
        if (pos < body) break; // overflow guard
    }
    if (ch < 1 || bits == 0 || data == nullptr)
    {
        error = "the WAV file has no usable audio";
        return false;
    }
    const bool isFloat = code == 3, isPcm = code == 1;
    if (!((isFloat && (bits == 32 || bits == 64)) || (isPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32))))
    {
        error = "unsupported WAV format (" + std::to_string(bits) + " bit, type " + std::to_string(code) + ")";
        return false;
    }
    const size_t bytes = (size_t) bits / 8, frames = dataLen / (bytes * (size_t) ch);
    mono.resize(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        double acc = 0;
        for (int c = 0; c < ch; ++c)
        {
            const uint8_t* p = data + (i * (size_t) ch + (size_t) c) * bytes;
            double v = 0;
            if (isFloat && bits == 32) { float f; std::memcpy(&f, p, 4); v = f; }
            else if (isFloat) { double d; std::memcpy(&d, p, 8); v = d; }
            else if (bits == 8) v = (p[0] - 128) / 128.0;
            else if (bits == 16) v = (int16_t) rd16(p) / 32768.0;
            else if (bits == 24)
            {
                int32_t s = (int32_t) (p[0] | (p[1] << 8) | (p[2] << 16));
                if (s & 0x800000) s -= 1 << 24;
                v = s / 8388608.0;
            }
            else v = (int32_t) rd32(p) / 2147483648.0;
            if (!std::isfinite(v)) v = 0;
            acc += v;
        }
        mono[i] = (float) (acc / ch);
    }
    return true;
}

bool readRawTable(const uint8_t* data, size_t numBytes, std::vector<float>& out)
{
    std::vector<float> m;
    int sr, clm;
    std::string err;
    if (!readWav(data, numBytes, m, sr, clm, err) || m.size() < (size_t) kTableFloats)
        return false;
    out.assign(m.begin(), m.begin() + kTableFloats);
    return true;
}

namespace
{
// periodic linear interpolation of one frame of `len` samples to kFrameSize samples
void resampleFrame(const float* src, int len, float* dst)
{
    if (len == kFrameSize) { std::copy(src, src + len, dst); return; }
    for (int i = 0; i < kFrameSize; ++i)
    {
        const double pos = (double) i * len / kFrameSize;
        const int i0 = (int) pos;
        const double f = pos - i0;
        dst[i] = (float) (src[i0 % len] * (1 - f) + src[(i0 + 1) % len] * f);
    }
}
} // namespace

bool convertToTable(const std::vector<float>& x, int hint, std::vector<float>& out, std::string& note, std::string& error)
{
    if (x.size() < 64)
    {
        error = "the file is too short to make a wavetable";
        return false;
    }
    std::vector<std::vector<float>> src; // source frames, each kFrameSize long
    if (hint <= 0 && x.size() % kFrameSize != 0 && x.size() < 2 * (size_t) kFrameSize)
    {
        src.assign(1, std::vector<float>(kFrameSize));
        resampleFrame(x.data(), (int) x.size(), src[0].data());
        note = "read as a single cycle (" + std::to_string(x.size()) + " samples); all 33 frames hold it";
    }
    else if (hint > 0 || x.size() % kFrameSize == 0)
    {
        const int fs = hint > 0 ? hint : kFrameSize;
        const size_t cnt = x.size() / (size_t) fs;
        if (cnt < 1)
        {
            error = "the file is shorter than one frame of " + std::to_string(fs) + " samples";
            return false;
        }
        for (size_t i = 0; i < cnt; ++i)
        {
            src.emplace_back(kFrameSize);
            resampleFrame(x.data() + i * (size_t) fs, fs, src.back().data());
        }
        note = std::to_string(cnt) + " frame" + (cnt == 1 ? "" : "s") + " of " + std::to_string(fs) + " samples, mapped to 33";
        if (const size_t rest = x.size() % (size_t) fs; rest != 0)
            note += " (the last " + std::to_string(rest) + " samples did not fill a frame and were left out)";
    }
    else
    {
        // a recording: 33 equal parts, each resampled to one frame; the first 128 samples blend in what follows the part
        const double span = (double) x.size() / kFrames;
        for (int f = 0; f < kFrames; ++f)
        {
            const double start = (kFrames > 1 ? (double) f * ((double) x.size() - span) / (kFrames - 1) : 0.0);
            std::vector<float> fr(kFrameSize);
            auto at = [&](double p) {
                const long i0 = (long) p;
                const double fr0 = p - i0;
                const long i1 = std::min<long>(i0 + 1, (long) x.size() - 1);
                return x[(size_t) std::min<long>(i0, (long) x.size() - 1)] * (1 - fr0) + x[(size_t) i1] * fr0;
            };
            for (int i = 0; i < kFrameSize; ++i) fr[(size_t) i] = (float) at(start + i * span / kFrameSize);
            const int k = kFrameSize / 16;
            for (int i = 0; i < k; ++i)
            {
                const double w = (double) i / (k - 1);
                fr[(size_t) i] = (float) (fr[(size_t) i] * w + at(start + span + i * span / kFrameSize) * (1 - w));
            }
            src.push_back(std::move(fr));
        }
        note = "no frame size found in the file, so the recording was cut into 33 equal parts";
    }

    out.assign((size_t) kTableFloats, 0.f);
    const size_t cnt = src.size();
    for (int f = 0; f < kFrames; ++f)
    {
        const double p = cnt > 1 ? (double) f * (double) (cnt - 1) / (kFrames - 1) : 0.0;
        const size_t j = (size_t) std::floor(p);
        const double fr = p - (double) j;
        float* d = &out[(size_t) f * kFrameSize];
        for (int i = 0; i < kFrameSize; ++i)
        {
            const float a = src[j][(size_t) i];
            const float v = (fr == 0 || j + 1 >= cnt) ? a : (float) (a * (1 - fr) + src[j + 1][(size_t) i] * fr);
            d[i] = std::isfinite(v) ? v : 0.f;
        }
        double mean = 0;
        for (int i = 0; i < kFrameSize; ++i) mean += d[i];
        mean /= kFrameSize;
        for (int i = 0; i < kFrameSize; ++i) d[i] = (float) (d[i] - mean);
    }
    float pk = 0;
    for (float v : out) pk = std::max(pk, std::fabs(v));
    if (pk < 1e-6f)
    {
        error = "the file is silent";
        out.clear();
        return false;
    }
    const float g = kPeak / pk;
    for (float& v : out) v *= g;
    return true;
}
} // namespace wtimport

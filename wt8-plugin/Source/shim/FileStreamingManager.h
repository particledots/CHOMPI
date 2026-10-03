// Stub: SD-card request queue is not used in the plugin. Wavetables are loaded from
// embedded WAV data by WT8Engine instead.
#pragma once
#include "daisy.h"
#include "fatfs.h"
#define MAX_SAMPLES_PER_CYCLE 2048
namespace daisy
{
static constexpr size_t kMaxFileStreamingSamps = 8192;
using SampleFifo = FIFO<int16_t, kMaxFileStreamingSamps>;
struct FileRequest
{
    enum class Type { OPEN, SEEK, MASS_READ, DUMMY };
    FileRequest() {}
    FileRequest(Type, FIL*, const char*, size_t, SampleFifo*, void*, float*) {}
};
class FileStreamingManager
{
  public:
    FIFO<FileRequest, 16> request_fifo;
    void Init(float) {}
    void ProcessRequests() {}
};
} // namespace daisy
using namespace daisy;

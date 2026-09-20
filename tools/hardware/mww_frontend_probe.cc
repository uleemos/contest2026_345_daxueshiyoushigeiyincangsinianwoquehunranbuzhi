// Host-only streaming frontend probe. Not a wake-word detector.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  char *end = nullptr;
  long chunk = strtol(argv[1], &end, 10);
  if (*end || chunk < 1 || chunk > 65536) return 2;
  FrontendConfig config{};
  FrontendFillConfigWithDefaults(&config);
  config.window.size_ms = 30;
  config.window.step_size_ms = 10;
  config.filterbank.num_channels = 40;
  config.filterbank.lower_band_limit = 125;
  config.filterbank.upper_band_limit = 7500;
  config.noise_reduction.smoothing_bits = 10;
  config.noise_reduction.even_smoothing = 0.025f;
  config.noise_reduction.odd_smoothing = 0.06f;
  config.noise_reduction.min_signal_remaining = 0.05f;
  config.pcan_gain_control.enable_pcan = 1;
  config.pcan_gain_control.strength = 0.95f;
  config.pcan_gain_control.offset = 80;
  config.pcan_gain_control.gain_bits = 21;
  config.log_scale.enable_log = 1;
  config.log_scale.scale_shift = 6;
  FrontendState state{};
  if (!FrontendPopulateState(&config, &state, 16000)) {
    FrontendFreeStateContents(&state);
    return 3;
  }
  std::vector<unsigned char> bytes(chunk * 2);
  std::vector<int16_t> pcm(chunk);
  int status = 0;
  while (true) {
    size_t count = fread(bytes.data(), 1, bytes.size(), stdin);
    if (!count) break;
    if (count % 2) { status = 4; break; }
    for (size_t i = 0; i < count / 2; ++i) {
      unsigned value = bytes[2*i] | (unsigned(bytes[2*i+1]) << 8);
      pcm[i] = static_cast<int16_t>(value < 32768 ? int(value) : int(value)-65536);
    }
    size_t pos = 0;
    while (pos < count / 2) {
      size_t used = 0;
      auto result = FrontendProcessSamples(&state, pcm.data()+pos, count/2-pos, &used);
      if (!used || used > count/2-pos) { status = 5; break; }
      pos += used;
      if (result.size && result.size != 40) { status = 6; break; }
      for (size_t i = 0; i < result.size; ++i)
        printf(i+1 == result.size ? "%u\n" : "%u,", result.values[i]);
    }
    if (status) break;
  }
  if (ferror(stdin) || ferror(stdout)) status = 7;
  FrontendFreeStateContents(&state);
  return status;
}

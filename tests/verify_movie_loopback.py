"""Verify a process-loopback WAV against decoded stereo s16le movie PCM.

Usage: python tests/verify_movie_loopback.py CAPTURE.wav REFERENCE.pcm
Requires NumPy and a 44.1 kHz process-loopback capture at unity gain.
"""
import sys
import wave

import numpy as np

rate = 44100
if len(sys.argv) != 3:
    raise SystemExit(__doc__)
with wave.open(sys.argv[1], 'rb') as wav:
    if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (rate, 2, 2):
        raise SystemExit('expected 44.1 kHz stereo 16-bit WAV')
    capture = np.frombuffer(wav.readframes(wav.getnframes()), '<i2').reshape(-1, 2)
source = np.fromfile(sys.argv[2], '<i2').reshape(-1, 2)
if len(source) < 3 * rate:
    raise SystemExit('reference must contain at least three seconds')

ref = source[2 * rate:2 * rate + rate // 2, 0].astype(float)
ref -= ref.mean()
if len(capture) < len(source):
    raise SystemExit('capture shorter than reference')
# Only starts that leave room for the complete reference are candidates.
search_end = len(capture) - len(source) + 2 * rate + len(ref)
signal = capture[:search_end, 0].astype(float)
if len(signal) < len(ref) or not np.any(ref):
    raise SystemExit('capture too short or reference alignment window silent')
size = 1 << (len(signal) + len(ref) - 2).bit_length()
cross = np.fft.irfft(np.fft.rfft(signal, size) * np.fft.rfft(ref[::-1], size), size)[len(ref)-1:len(signal)]
sums = np.r_[0, np.cumsum(signal)]
squares = np.r_[0, np.cumsum(signal * signal)]
energy = squares[len(ref):] - squares[:-len(ref)] - (sums[len(ref):] - sums[:-len(ref)]) ** 2 / len(ref)
scores = cross / (np.linalg.norm(ref) * np.sqrt(np.maximum(energy, 1)))
index = int(np.argmax(scores))
offset = index - 2 * rate
if not np.isfinite(scores[index]) or scores[index] < 0.99 or offset < 0 or offset + len(source) > len(capture):
    raise SystemExit('complete aligned movie audio was not found in capture')
maximum = errors = samples = 0
for start in range(0, len(source), 10 * rate):
    end = min(start + 10 * rate, len(source))
    delta = capture[offset + start:offset + end].astype(np.int32) - source[start:end].astype(np.int32)
    maximum = max(maximum, int(np.max(np.abs(delta))))
    errors += int(np.count_nonzero(np.abs(delta) > 1))
    samples += delta.size
print(f'samples={samples} max_difference={maximum} samples_over_one={errors}')
if errors:
    raise SystemExit('movie loopback differs from decoded PCM')

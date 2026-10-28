#!/usr/bin/env python3

"""Compare two audio previews using chroma and envelope similarity."""

import argparse
import json
import wave

import numpy as np


def read_wav(path: str) -> tuple[np.ndarray, int]:
    with wave.open(path, "rb") as source:
        channels = source.getnchannels()
        width = source.getsampwidth()
        sample_rate = source.getframerate()
        frames = source.readframes(source.getnframes())
    if width != 2:
        raise ValueError(f"{path}: expected 16-bit PCM")

    samples = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
    if channels > 1:
        samples = samples.reshape(-1, channels).mean(axis=1)
    return samples, sample_rate


def frame_signal(signal: np.ndarray, frame_size: int, hop: int) -> np.ndarray:
    if len(signal) < frame_size:
        signal = np.pad(signal, (0, frame_size - len(signal)))
    frame_count = 1 + (len(signal) - frame_size) // hop
    shape = (frame_count, frame_size)
    strides = (signal.strides[0] * hop, signal.strides[0])
    return np.lib.stride_tricks.as_strided(signal, shape=shape, strides=strides)


def chromagram(signal: np.ndarray, sample_rate: int) -> tuple[np.ndarray, np.ndarray]:
    frame_size = 2048
    hop = 512
    frames = frame_signal(signal, frame_size, hop)
    window = np.hanning(frame_size).astype(np.float32)
    spectrum = np.abs(np.fft.rfft(frames * window, axis=1))
    frequencies = np.fft.rfftfreq(frame_size, 1.0 / sample_rate)

    valid = (frequencies >= 55.0) & (frequencies <= 2200.0)
    midi = 69.0 + 12.0 * np.log2(np.maximum(frequencies[valid], 1e-9) / 440.0)
    pitch_class = np.mod(np.rint(midi).astype(np.int32), 12)

    chroma = np.zeros((spectrum.shape[0], 12), dtype=np.float32)
    energies = spectrum[:, valid]
    for pitch in range(12):
        chroma[:, pitch] = energies[:, pitch_class == pitch].sum(axis=1)
    chroma /= np.maximum(np.linalg.norm(chroma, axis=1, keepdims=True), 1e-8)

    envelope = np.sqrt(np.mean(frames * frames, axis=1))
    return chroma, envelope


def finite_correlation(left: np.ndarray, right: np.ndarray) -> float:
    if left.size < 2 or right.size < 2:
        return 0.0
    left = left - left.mean()
    right = right - right.mean()
    denominator = float(np.linalg.norm(left) * np.linalg.norm(right))
    if denominator < 1e-8:
        return 1.0 if np.allclose(left, right) else 0.0
    return float(np.dot(left, right) / denominator)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_wav")
    parser.add_argument("comparison_wav")
    args = parser.parse_args()

    source, source_rate = read_wav(args.source_wav)
    comparison, comparison_rate = read_wav(args.comparison_wav)
    if source_rate != comparison_rate:
        raise ValueError("sample rates differ")

    sample_count = min(len(source), len(comparison))
    source = source[:sample_count]
    comparison = comparison[:sample_count]
    source_chroma, source_envelope = chromagram(source, source_rate)
    comparison_chroma, comparison_envelope = chromagram(comparison, source_rate)

    active = source_envelope > max(float(np.percentile(source_envelope, 20)), 1e-5)
    cosine = np.sum(source_chroma[active] * comparison_chroma[active], axis=1)
    result = {
        "source": args.source_wav,
        "comparison": args.comparison_wav,
        "duration_seconds": round(sample_count / source_rate, 3),
        "chroma_similarity_mean": round(float(cosine.mean()), 6),
        "chroma_similarity_p10": round(float(np.percentile(cosine, 10)), 6),
        "envelope_correlation": round(
            finite_correlation(source_envelope, comparison_envelope), 6),
        "source_rms": round(float(np.sqrt(np.mean(source * source))), 8),
        "comparison_rms": round(float(np.sqrt(np.mean(comparison * comparison))), 8),
        "waveform_mae": round(
            float(np.mean(np.abs(source - comparison))), 8),
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

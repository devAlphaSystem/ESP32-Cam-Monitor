#pragma once

#include <Arduino.h>

static const char LIVE_PLAYER_SCRIPT[] PROGMEM = R"CWJS((function (global) {
  'use strict';

  const RECORD_STATUS = 1;
  const RECORD_RTP = 2;
  const RECORD_ERROR = 3;
  const START_CODE = new Uint8Array([0, 0, 0, 1]);

  function concatBytes(parts) {
    let length = 0;
    for (const part of parts) length += part.byteLength;
    const output = new Uint8Array(length);
    let offset = 0;
    for (const part of parts) {
      output.set(part, offset);
      offset += part.byteLength;
    }
    return output;
  }

  function ascii(text) {
    const bytes = new Uint8Array(text.length);
    for (let index = 0; index < text.length; index++) bytes[index] = text.charCodeAt(index) & 0xff;
    return bytes;
  }

  function u16(value) {
    return new Uint8Array([(value >>> 8) & 0xff, value & 0xff]);
  }

  function u24(value) {
    return new Uint8Array([(value >>> 16) & 0xff, (value >>> 8) & 0xff, value & 0xff]);
  }

  function u32(value) {
    const normalized = Number(value) >>> 0;
    return new Uint8Array([
      (normalized >>> 24) & 0xff,
      (normalized >>> 16) & 0xff,
      (normalized >>> 8) & 0xff,
      normalized & 0xff
    ]);
  }

  function i32(value) {
    return u32(value < 0 ? 0x100000000 + value : value);
  }

  function u64(value) {
    const normalized = Math.max(0, Math.floor(value));
    const high = Math.floor(normalized / 0x100000000);
    const low = normalized >>> 0;
    return concatBytes([u32(high), u32(low)]);
  }

  function fixed16(value) {
    return u32(Math.round(value * 65536));
  }

  function fixed8(value) {
    return u16(Math.round(value * 256));
  }

  function box(type, ...payloads) {
    const size = 8 + payloads.reduce((sum, payload) => sum + payload.byteLength, 0);
    return concatBytes([u32(size), ascii(type), ...payloads]);
  }

  function fullBox(type, version, flags, ...payloads) {
    return box(type, new Uint8Array([version & 0xff]), u24(flags), ...payloads);
  }

  function removeEmulationPrevention(data) {
    const output = [];
    for (let index = 0; index < data.length; index++) {
      if (
        index >= 2 &&
        data[index] === 0x03 &&
        data[index - 1] === 0x00 &&
        data[index - 2] === 0x00
      ) {
        continue;
      }
      output.push(data[index]);
    }
    return new Uint8Array(output);
  }

  class BitReader {
    constructor(data) {
      this.data = data;
      this.bitOffset = 0;
    }

    readBit() {
      if (this.bitOffset >= this.data.length * 8) throw new Error('SPS truncado');
      const byte = this.data[this.bitOffset >>> 3];
      const bit = (byte >>> (7 - (this.bitOffset & 7))) & 1;
      this.bitOffset++;
      return bit;
    }

    readBits(count) {
      let value = 0;
      for (let index = 0; index < count; index++) value = value * 2 + this.readBit();
      return value;
    }

    readUE() {
      let leadingZeroBits = 0;
      while (this.readBit() === 0) {
        leadingZeroBits++;
        if (leadingZeroBits > 31) throw new Error('Exp-Golomb inválido');
      }
      return leadingZeroBits === 0
        ? 0
        : Math.pow(2, leadingZeroBits) - 1 + this.readBits(leadingZeroBits);
    }

    readSE() {
      const codeNum = this.readUE();
      const magnitude = Math.ceil(codeNum / 2);
      return codeNum % 2 === 0 ? -magnitude : magnitude;
    }
  }

  function skipScalingList(reader, size) {
    let lastScale = 8;
    let nextScale = 8;
    for (let index = 0; index < size; index++) {
      if (nextScale !== 0) {
        const deltaScale = reader.readSE();
        nextScale = (lastScale + deltaScale + 256) % 256;
      }
      lastScale = nextScale === 0 ? lastScale : nextScale;
    }
  }

  function parseSps(sps) {
    if (!sps || sps.length < 4 || (sps[0] & 0x1f) !== 7) throw new Error('SPS H.264 inválido');
    const profileIdc = sps[1];
    const compatibility = sps[2];
    const levelIdc = sps[3];
    const rbsp = removeEmulationPrevention(sps.subarray(1));
    const reader = new BitReader(rbsp);

    const parsedProfileIdc = reader.readBits(8);
    reader.readBits(8);
    reader.readBits(8);
    reader.readUE();

    let chromaFormatIdc = 1;
    let separateColourPlaneFlag = 0;
    const extendedProfiles = new Set([100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135]);
    if (extendedProfiles.has(parsedProfileIdc)) {
      chromaFormatIdc = reader.readUE();
      if (chromaFormatIdc === 3) separateColourPlaneFlag = reader.readBit();
      reader.readUE();
      reader.readUE();
      reader.readBit();
      const scalingMatrixPresent = reader.readBit();
      if (scalingMatrixPresent) {
        const count = chromaFormatIdc !== 3 ? 8 : 12;
        for (let index = 0; index < count; index++) {
          if (reader.readBit()) skipScalingList(reader, index < 6 ? 16 : 64);
        }
      }
    }

    reader.readUE();
    const picOrderCntType = reader.readUE();
    if (picOrderCntType === 0) {
      reader.readUE();
    } else if (picOrderCntType === 1) {
      reader.readBit();
      reader.readSE();
      reader.readSE();
      const cycle = reader.readUE();
      for (let index = 0; index < cycle; index++) reader.readSE();
    }

    reader.readUE();
    reader.readBit();
    const picWidthInMbsMinus1 = reader.readUE();
    const picHeightInMapUnitsMinus1 = reader.readUE();
    const frameMbsOnlyFlag = reader.readBit();
    if (!frameMbsOnlyFlag) reader.readBit();
    reader.readBit();
    const frameCroppingFlag = reader.readBit();

    let cropLeft = 0;
    let cropRight = 0;
    let cropTop = 0;
    let cropBottom = 0;
    if (frameCroppingFlag) {
      cropLeft = reader.readUE();
      cropRight = reader.readUE();
      cropTop = reader.readUE();
      cropBottom = reader.readUE();
    }

    const chromaArrayType = separateColourPlaneFlag ? 0 : chromaFormatIdc;
    let cropUnitX = 1;
    let cropUnitY = 2 - frameMbsOnlyFlag;
    if (chromaArrayType === 1) {
      cropUnitX = 2;
      cropUnitY = 2 * (2 - frameMbsOnlyFlag);
    } else if (chromaArrayType === 2) {
      cropUnitX = 2;
      cropUnitY = 2 - frameMbsOnlyFlag;
    }

    const width = (picWidthInMbsMinus1 + 1) * 16 - (cropLeft + cropRight) * cropUnitX;
    const height = (2 - frameMbsOnlyFlag) * (picHeightInMapUnitsMinus1 + 1) * 16
      - (cropTop + cropBottom) * cropUnitY;
    const hex = value => value.toString(16).padStart(2, '0');

    return {
      width,
      height,
      profileIdc,
      compatibility,
      levelIdc,
      codec: 'avc1.' + hex(profileIdc) + hex(compatibility) + hex(levelIdc)
    };
  }

  function makeAvcC(sps, pps) {
    return box('avcC', concatBytes([
      new Uint8Array([1, sps[1], sps[2], sps[3], 0xff, 0xe1]),
      u16(sps.byteLength),
      sps,
      new Uint8Array([1]),
      u16(pps.byteLength),
      pps
    ]));
  }

  function makeFtyp() {
    return box('ftyp', ascii('isom'), u32(1), ascii('isomiso2avc1mp41'));
  }

  const IDENTITY_MATRIX = concatBytes([
    fixed16(1), fixed16(0), u32(0),
    fixed16(0), fixed16(1), u32(0),
    fixed16(0), fixed16(0), u32(0x40000000)
  ]);

  function makeMvhd(timescale) {
    return fullBox('mvhd', 0, 0,
      u32(0), u32(0), u32(timescale), u32(0), fixed16(1), fixed8(1),
      new Uint8Array(10), IDENTITY_MATRIX, new Uint8Array(24), u32(2));
  }

  function makeTkhd(width, height) {
    return fullBox('tkhd', 0, 0x000007,
      u32(0), u32(0), u32(1), u32(0), u32(0), new Uint8Array(8),
      u16(0), u16(0), fixed8(0), u16(0), IDENTITY_MATRIX,
      fixed16(width), fixed16(height));
  }

  function makeMdhd(timescale) {
    return fullBox('mdhd', 0, 0, u32(0), u32(0), u32(timescale), u32(0), u16(0x55c4), u16(0));
  }

  function makeHdlr() {
    return fullBox('hdlr', 0, 0, u32(0), ascii('vide'), new Uint8Array(12), ascii('Camera Watchdog\0'));
  }

  function makeVmhd() {
    return fullBox('vmhd', 0, 1, u16(0), u16(0), u16(0), u16(0));
  }

  function makeDinf() {
    const url = fullBox('url ', 0, 1);
    const dref = fullBox('dref', 0, 0, u32(1), url);
    return box('dinf', dref);
  }

  function makeAvc1(width, height, sps, pps) {
    const compressor = new Uint8Array(32);
    const name = ascii('Camera Watchdog');
    compressor[0] = Math.min(31, name.length);
    compressor.set(name.subarray(0, 31), 1);
    const header = concatBytes([
      new Uint8Array(6), u16(1), new Uint8Array(16),
      u16(width), u16(height), u32(0x00480000), u32(0x00480000),
      u32(0), u16(1), compressor, u16(0x0018), u16(0xffff)
    ]);
    return box('avc1', header, makeAvcC(sps, pps));
  }

  function makeStbl(width, height, sps, pps) {
    const stsd = fullBox('stsd', 0, 0, u32(1), makeAvc1(width, height, sps, pps));
    const emptyTable = type => fullBox(type, 0, 0, u32(0));
    const emptySampleSize = fullBox('stsz', 0, 0, u32(0), u32(0));
    return box('stbl', stsd, emptyTable('stts'), emptyTable('stsc'), emptySampleSize, emptyTable('stco'));
  }

  function makeMinf(width, height, sps, pps) {
    return box('minf', makeVmhd(), makeDinf(), makeStbl(width, height, sps, pps));
  }

  function makeMdia(timescale, width, height, sps, pps) {
    return box('mdia', makeMdhd(timescale), makeHdlr(), makeMinf(width, height, sps, pps));
  }

  function makeTrak(timescale, width, height, sps, pps) {
    return box('trak', makeTkhd(width, height), makeMdia(timescale, width, height, sps, pps));
  }

  function makeMvex() {
    return box('mvex', fullBox('trex', 0, 0, u32(1), u32(1), u32(0), u32(0), u32(0)));
  }

  function makeInitSegment(timescale, sps, pps) {
    const parsed = parseSps(sps);
    return {
      codec: parsed.codec,
      width: parsed.width,
      height: parsed.height,
      data: concatBytes([
        makeFtyp(),
        box('moov', makeMvhd(timescale), makeTrak(timescale, parsed.width, parsed.height, sps, pps), makeMvex())
      ])
    };
  }

  function makeMfhd(sequence) {
    return fullBox('mfhd', 0, 0, u32(sequence));
  }

  function makeTfhd() {
    return fullBox('tfhd', 0, 0x020000, u32(1));
  }

  function makeTfdt(baseDecodeTime) {
    return fullBox('tfdt', 1, 0, u64(baseDecodeTime));
  }

  function makeTrun(duration, size, keyframe, compositionOffset, dataOffset) {
    const flags = keyframe ? 0x02000000 : 0x01010000;
    return fullBox('trun', 1, 0x000f01,
      u32(1), i32(dataOffset), u32(duration), u32(size), u32(flags), i32(compositionOffset));
  }

  function makeFragment(sequence, baseDecodeTime, duration, compositionOffset, keyframe, sampleData) {
    let trun = makeTrun(duration, sampleData.byteLength, keyframe, compositionOffset, 0);
    let traf = box('traf', makeTfhd(), makeTfdt(baseDecodeTime), trun);
    let moof = box('moof', makeMfhd(sequence), traf);
    trun = makeTrun(duration, sampleData.byteLength, keyframe, compositionOffset, moof.byteLength + 8);
    traf = box('traf', makeTfhd(), makeTfdt(baseDecodeTime), trun);
    moof = box('moof', makeMfhd(sequence), traf);
    return concatBytes([moof, box('mdat', sampleData)]);
  }

  function accessUnitToAvcc(nalus) {
    const parts = [];
    for (const nalu of nalus) {
      if (!nalu || !nalu.byteLength) continue;
      const type = nalu[0] & 0x1f;
      // avc1 keeps SPS/PPS in avcC. AUD, end markers and filler do not belong
      // in the compressed video sample and upset some embedded-camera streams.
      if (type === 7 || type === 8 || type === 9 || type === 10 || type === 11 || type === 12) continue;
      parts.push(u32(nalu.byteLength), nalu);
    }
    return concatBytes(parts);
  }

  function decodeBase64(value) {
    const binary = atob(value);
    const output = new Uint8Array(binary.length);
    for (let index = 0; index < binary.length; index++) output[index] = binary.charCodeAt(index);
    return output;
  }

  class SourceBufferQueue {
    constructor(video, callbacks) {
      this.video = video;
      this.callbacks = callbacks;
      this.mediaSource = null;
      this.sourceBuffer = null;
      this.queue = [];
      this.currentItem = null;
      this.objectUrl = '';
      this.openPromise = null;
      this.destroyed = false;
      this.lastCleanupAt = 0;
      this.reset();
    }

    reset() {
      this.destroy();
      this.destroyed = false;
      this.queue = [];
      this.currentItem = null;
      this.mediaSource = new MediaSource();
      this.objectUrl = URL.createObjectURL(this.mediaSource);
      this.video.src = this.objectUrl;
      this.video.muted = true;
      this.video.playsInline = true;
      this.openPromise = new Promise((resolve, reject) => {
        const onOpen = () => {
          cleanup();
          resolve();
        };
        const onError = () => {
          cleanup();
          reject(new Error('MediaSource não abriu'));
        };
        const cleanup = () => {
          this.mediaSource.removeEventListener('sourceopen', onOpen);
          this.mediaSource.removeEventListener('sourceended', onError);
          this.mediaSource.removeEventListener('sourceclose', onError);
        };
        this.mediaSource.addEventListener('sourceopen', onOpen, { once: true });
        this.mediaSource.addEventListener('sourceended', onError, { once: true });
        this.mediaSource.addEventListener('sourceclose', onError, { once: true });
      });
    }

    mediaErrorDetail() {
      const mediaError = this.video && this.video.error;
      if (!mediaError) return '';
      const names = {
        1: 'MEDIA_ERR_ABORTED',
        2: 'MEDIA_ERR_NETWORK',
        3: 'MEDIA_ERR_DECODE',
        4: 'MEDIA_ERR_SRC_NOT_SUPPORTED'
      };
      const parts = [names[mediaError.code] || ('código ' + mediaError.code)];
      if (mediaError.message) parts.push(mediaError.message);
      return parts.join(' · ');
    }

    makeBufferError(label, original) {
      let detail = 'Falha ao anexar ' + (label || 'dados H.264') + ' ao buffer de vídeo';
      const mediaDetail = this.mediaErrorDetail();
      if (mediaDetail) detail += ' (' + mediaDetail + ')';
      if (original && original.message && detail.indexOf(original.message) < 0) {
        detail += ': ' + original.message;
      }
      const error = new Error(detail);
      if (original && original.name) error.name = original.name;
      return error;
    }

    async initialize(codec, initSegment) {
      await this.openPromise;
      if (this.destroyed) throw new Error('Player encerrado');
      const mime = 'video/mp4; codecs="' + codec + '"';
      if (!MediaSource.isTypeSupported(mime)) throw new Error('O navegador não suporta ' + codec + ' via MSE');
      this.sourceBuffer = this.mediaSource.addSourceBuffer(mime);
      this.sourceBuffer.mode = 'segments';
      this.sourceBuffer.addEventListener('updateend', () => {
        const completed = this.currentItem;
        this.currentItem = null;
        if (completed && completed.resolve) completed.resolve();
        this.syncToLiveEdge();
        this.pump();
      });
      this.sourceBuffer.addEventListener('error', () => {
        const failed = this.currentItem;
        this.currentItem = null;
        const error = this.makeBufferError(failed && failed.label);
        if (failed && failed.reject) failed.reject(error);
        if (this.callbacks.onError) this.callbacks.onError(error);
      });
      await this.appendAndWait(initSegment, 'o segmento inicial MP4');
      try {
        const playAttempt = this.video.play();
        if (playAttempt && typeof playAttempt.catch === 'function') playAttempt.catch(function () {});
      } catch (_) {
        // O elemento permanece com controles; um clique do usuário libera autoplay quando necessário.
      }
    }

    enqueue(data, label = 'um fragmento de vídeo') {
      if (this.destroyed || !data || !data.byteLength) return;
      this.queue.push({ data, label, resolve: null, reject: null });
      this.pump();
    }

    appendAndWait(data, label) {
      if (this.destroyed || !data || !data.byteLength) {
        return Promise.reject(new Error('Player encerrado'));
      }
      return new Promise((resolve, reject) => {
        this.queue.push({ data, label, resolve, reject });
        this.pump();
      });
    }

    syncToLiveEdge() {
      if (!this.sourceBuffer || !this.sourceBuffer.buffered.length) return;
      try {
        const last = this.sourceBuffer.buffered.length - 1;
        const end = this.sourceBuffer.buffered.end(last);
        const lag = end - this.video.currentTime;
        if ((this.video.currentTime === 0 && end > 0.2) || lag > 2.2) {
          this.video.currentTime = Math.max(0, end - 0.25);
        }
      } catch (_) {
        // O intervalo pode mudar enquanto o SourceBuffer conclui uma operação.
      }
    }

    pump() {
      if (
        this.destroyed ||
        !this.sourceBuffer ||
        this.sourceBuffer.updating ||
        this.currentItem ||
        this.queue.length === 0 ||
        this.mediaSource.readyState !== 'open'
      ) return;

      const now = Date.now();
      if (
        now - this.lastCleanupAt > 3000 &&
        this.video.currentTime > 12 &&
        this.sourceBuffer.buffered.length
      ) {
        const removeUntil = Math.min(this.video.currentTime - 8, this.sourceBuffer.buffered.end(0) - 2);
        if (removeUntil > this.sourceBuffer.buffered.start(0)) {
          this.lastCleanupAt = now;
          try {
            this.sourceBuffer.remove(0, removeUntil);
            return;
          } catch (_) {
            // Continua com o próximo fragmento se o navegador recusar a limpeza naquele instante.
          }
        }
      }

      const next = this.queue.shift();
      this.currentItem = next;
      try {
        this.sourceBuffer.appendBuffer(next.data);
      } catch (original) {
        this.currentItem = null;
        const error = this.makeBufferError(next.label, original);
        if (next.reject) next.reject(error);
        if (this.callbacks.onError) this.callbacks.onError(error);
      }
    }

    destroy() {
      this.destroyed = true;
      const stopped = new Error('Player encerrado');
      if (this.currentItem && this.currentItem.reject) this.currentItem.reject(stopped);
      for (const pending of this.queue || []) {
        if (pending.reject) pending.reject(stopped);
      }
      this.currentItem = null;
      this.queue = [];
      if (this.sourceBuffer) {
        try { this.sourceBuffer.abort(); } catch (_) {}
      }
      this.sourceBuffer = null;
      if (this.mediaSource && this.mediaSource.readyState === 'open') {
        try { this.mediaSource.endOfStream(); } catch (_) {}
      }
      if (this.objectUrl) URL.revokeObjectURL(this.objectUrl);
      this.objectUrl = '';
      if (this.video) {
        this.video.removeAttribute('src');
        try { this.video.load(); } catch (_) {}
      }
    }
  }

  class FragmentedMp4Muxer {
    constructor(video, callbacks) {
      this.video = video;
      this.callbacks = callbacks;
      this.timescale = 90000;
      this.sps = null;
      this.pps = null;
      this.sequence = 1;
      this.decodeTime = 0;
      this.firstPts = null;
      this.lastPts = null;
      this.frameRateHint = 24;
      this.durationEstimate = 3750;
      this.durationSamples = [];
      this.initialized = false;
      this.waitingForKeyframe = true;
      this.queue = new SourceBufferQueue(video, callbacks);
    }

    setClockRate(clockRate) {
      if (Number.isFinite(clockRate) && clockRate >= 1000) {
        this.timescale = Math.round(clockRate);
        this.durationEstimate = Math.max(1, Math.round(this.timescale / this.frameRateHint));
      }
    }

    setFrameRate(frameRate) {
      if (Number.isFinite(frameRate) && frameRate >= 1 && frameRate <= 120) {
        this.frameRateHint = frameRate;
        this.durationEstimate = Math.max(1, Math.round(this.timescale / frameRate));
      }
    }

    setParameterSets(sps, pps) {
      if (sps && sps.byteLength) this.sps = new Uint8Array(sps);
      if (pps && pps.byteLength) this.pps = new Uint8Array(pps);
    }

    inspectNalus(nalus) {
      let keyframe = false;
      for (const nalu of nalus) {
        const type = nalu[0] & 0x1f;
        if (type === 7) this.sps = new Uint8Array(nalu);
        if (type === 8) this.pps = new Uint8Array(nalu);
        if (type === 5) keyframe = true;
      }
      return keyframe;
    }

    updateDuration(pts) {
      if (this.lastPts != null) {
        const diff = pts - this.lastPts;
        const minimum = this.timescale / 120;
        const maximum = this.timescale / 2;
        if (diff > minimum && diff < maximum) {
          this.durationSamples.push(diff);
          if (this.durationSamples.length > 20) this.durationSamples.shift();
          const sorted = [...this.durationSamples].sort((a, b) => a - b);
          this.durationEstimate = Math.max(1, Math.round(sorted[Math.floor(sorted.length / 2)]));
        }
      }
      this.lastPts = pts;
    }

    async pushAccessUnit(nalus, pts) {
      if (!nalus || !nalus.length) return;
      const keyframe = this.inspectNalus(nalus);
      this.updateDuration(pts);
      if (this.waitingForKeyframe && !keyframe) return;
      if (!this.sps || !this.pps) return;

      if (!this.initialized) {
        if (!keyframe) return;
        const init = makeInitSegment(this.timescale, this.sps, this.pps);
        await this.queue.initialize(init.codec, init.data);
        this.initialized = true;
        this.waitingForKeyframe = false;
        if (this.callbacks.onVideoInfo) {
          this.callbacks.onVideoInfo({
            codec: init.codec,
            width: init.width,
            height: init.height,
            timescale: this.timescale
          });
        }
      }

      if (this.firstPts == null) this.firstPts = pts;
      const ptsRelative = pts - this.firstPts;
      const compositionOffset = Math.max(-0x7fffffff, Math.min(0x7fffffff, Math.round(ptsRelative - this.decodeTime)));
      const duration = this.durationEstimate;
      const sample = accessUnitToAvcc(nalus);
      if (!sample.byteLength) return;
      const fragment = makeFragment(
        this.sequence++,
        this.decodeTime,
        duration,
        compositionOffset,
        keyframe,
        sample
      );
      this.decodeTime += duration;
      this.queue.enqueue(fragment);
    }

    reset() {
      this.queue.reset();
      this.sequence = 1;
      this.decodeTime = 0;
      this.firstPts = null;
      this.lastPts = null;
      this.durationSamples = [];
      this.initialized = false;
      this.waitingForKeyframe = true;
    }

    destroy() {
      this.queue.destroy();
    }
  }

  class RtpH264Assembler {
    constructor(onAccessUnit, onDrop, onMarkerCorrection) {
      this.onAccessUnit = onAccessUnit;
      this.onDrop = onDrop;
      this.onMarkerCorrection = onMarkerCorrection;
      this.currentTimestamp = null;
      this.currentNalus = [];
      this.currentFuParts = null;
      this.currentFuTimestamp = null;
      this.lastSequence = null;
      this.expectedPayloadType = null;
      this.lastRawTimestamp = null;
      this.timestampCycles = 0;
      this.damaged = false;
      this.markerSeen = false;
    }

    setPayloadType(payloadType) {
      const value = Number(payloadType);
      this.expectedPayloadType = Number.isInteger(value) && value >= 0 && value <= 127
        ? value
        : null;
    }

    unwrapTimestamp(raw) {
      if (this.lastRawTimestamp != null) {
        const difference = raw - this.lastRawTimestamp;
        if (difference < -0x80000000) this.timestampCycles += 0x100000000;
        if (difference > 0x80000000) this.timestampCycles -= 0x100000000;
      }
      this.lastRawTimestamp = raw;
      return this.timestampCycles + raw;
    }

    finishAccessUnit() {
      if (this.currentTimestamp == null) return;
      if (this.currentFuParts) this.damaged = true;
      if (!this.damaged && this.currentNalus.length) {
        this.onAccessUnit(this.currentNalus, this.currentTimestamp);
      } else if (this.damaged && this.onDrop) {
        this.onDrop();
      }
      this.currentNalus = [];
      this.currentFuParts = null;
      this.currentFuTimestamp = null;
      this.damaged = false;
      this.markerSeen = false;
    }

    appendAggregation(payload, startPosition) {
      let position = startPosition;
      while (position + 2 <= payload.length) {
        const length = (payload[position] << 8) | payload[position + 1];
        position += 2;
        if (!length) {
          let onlyPadding = true;
          for (let index = position; index < payload.length; index++) {
            if (payload[index] !== 0) { onlyPadding = false; break; }
          }
          if (onlyPadding) break;
          this.damaged = true;
          return;
        }
        if (position + length > payload.length) {
          this.damaged = true;
          return;
        }
        this.currentNalus.push(new Uint8Array(payload.subarray(position, position + length)));
        position += length;
      }
    }

    push(packet) {
      if (!packet || packet.length < 12) return;
      const version = packet[0] >>> 6;
      if (version !== 2) return;
      const padding = (packet[0] & 0x20) !== 0;
      const extension = (packet[0] & 0x10) !== 0;
      const csrcCount = packet[0] & 0x0f;
      const marker = (packet[1] & 0x80) !== 0;
      const payloadType = packet[1] & 0x7f;
      const sequence = (packet[2] << 8) | packet[3];

      if (this.lastSequence != null && ((this.lastSequence + 1) & 0xffff) !== sequence) {
        this.damaged = true;
        this.currentFuParts = null;
      }
      this.lastSequence = sequence;

      if (this.expectedPayloadType != null && payloadType !== this.expectedPayloadType) return;

      const rawTimestamp = (
        packet[4] * 0x1000000 +
        (packet[5] << 16) +
        (packet[6] << 8) +
        packet[7]
      ) >>> 0;
      const timestamp = this.unwrapTimestamp(rawTimestamp);
      let offset = 12 + csrcCount * 4;
      if (extension) {
        if (offset + 4 > packet.length) return;
        const extensionWords = (packet[offset + 2] << 8) | packet[offset + 3];
        offset += 4 + extensionWords * 4;
      }
      let end = packet.length;
      if (padding) {
        const paddingLength = packet[packet.length - 1];
        if (paddingLength > packet.length - offset) return;
        end -= paddingLength;
      }
      if (offset >= end) return;

      if (this.currentTimestamp != null && timestamp !== this.currentTimestamp) {
        this.finishAccessUnit();
        this.currentTimestamp = null;
      } else if (this.currentTimestamp != null && this.markerSeen) {
        // Some inexpensive cameras set M=1 at the end of every NAL or slice,
        // instead of only on the final packet of the access unit. RTP timestamp
        // is the reliable delimiter here; keep collecting the same picture.
        this.markerSeen = false;
        if (this.onMarkerCorrection) this.onMarkerCorrection();
      }
      if (this.currentTimestamp == null) this.currentTimestamp = timestamp;

      const payload = packet.subarray(offset, end);
      const nalType = payload[0] & 0x1f;
      if (nalType >= 1 && nalType <= 23) {
        this.currentNalus.push(new Uint8Array(payload));
      } else if (nalType === 24) {
        this.appendAggregation(payload, 1);
      } else if (nalType === 25 && payload.length >= 3) {
        // STAP-B adds a two-byte decoding-order number before the first NAL.
        this.appendAggregation(payload, 3);
      } else if ((nalType === 28 || nalType === 29) && payload.length >= (nalType === 29 ? 4 : 2)) {
        const start = (payload[1] & 0x80) !== 0;
        const finish = (payload[1] & 0x40) !== 0;
        const reconstructed = (payload[0] & 0xe0) | (payload[1] & 0x1f);
        const fragmentOffset = nalType === 29 ? 4 : 2;
        if (start) {
          this.currentFuParts = [new Uint8Array([reconstructed]), new Uint8Array(payload.subarray(fragmentOffset))];
          this.currentFuTimestamp = timestamp;
        } else if (this.currentFuParts && this.currentFuTimestamp === timestamp) {
          this.currentFuParts.push(new Uint8Array(payload.subarray(fragmentOffset)));
        } else {
          this.damaged = true;
        }
        if (finish && this.currentFuParts && this.currentFuTimestamp === timestamp) {
          this.currentNalus.push(concatBytes(this.currentFuParts));
          this.currentFuParts = null;
          this.currentFuTimestamp = null;
        }
      } else if (nalType === 26 || nalType === 27) {
        // MTAP16/24 require per-NAL timestamp offsets. They are uncommon in IP
        // cameras and cannot be merged safely without a full interleaving queue.
        this.damaged = true;
      }

      // Deliberately do not flush on marker. The next RTP timestamp closes the
      // access unit. This adds at most one frame of latency and tolerates the
      // non-standard marker behavior observed in several embedded cameras.
      if (marker) this.markerSeen = true;
    }

    flush() {
      this.finishAccessUnit();
      this.currentTimestamp = null;
    }
  }

  class RecordParser {
    constructor(onRecord) {
      this.onRecord = onRecord;
      this.buffer = new Uint8Array(0);
      this.offset = 0;
    }

    push(chunk) {
      if (!chunk || !chunk.byteLength) return;
      if (this.offset === this.buffer.byteLength) {
        this.buffer = new Uint8Array(chunk);
        this.offset = 0;
      } else {
        this.buffer = concatBytes([this.buffer.subarray(this.offset), chunk]);
        this.offset = 0;
      }

      while (this.buffer.byteLength - this.offset >= 5) {
        const type = this.buffer[this.offset];
        const length = (
          this.buffer[this.offset + 1] * 0x1000000 +
          (this.buffer[this.offset + 2] << 16) +
          (this.buffer[this.offset + 3] << 8) +
          this.buffer[this.offset + 4]
        ) >>> 0;
        if (length > 1024 * 1024) throw new Error('Registro de vídeo excedeu o limite');
        if (this.buffer.byteLength - this.offset < 5 + length) break;
        const payload = this.buffer.slice(this.offset + 5, this.offset + 5 + length);
        this.offset += 5 + length;
        this.onRecord(type, payload);
      }

      if (this.offset > 65536 || this.offset === this.buffer.byteLength) {
        this.buffer = this.buffer.slice(this.offset);
        this.offset = 0;
      }
    }
  }

  class CameraWatchdogLivePlayer {
    constructor(options) {
      if (!options || !options.video) throw new Error('Elemento de vídeo ausente');
      this.video = options.video;
      this.onStatus = options.onStatus || function () {};
      this.onStats = options.onStats || function () {};
      this.onError = options.onError || function () {};
      this.onVideoInfo = options.onVideoInfo || function () {};
      this.onPlaybackReady = options.onPlaybackReady || function () {};
      this.abortController = null;
      this.running = false;
      this.reader = null;
      this.muxer = null;
      this.muxChain = Promise.resolve();
      this.assembler = null;
      this.stats = this.makeStats();
      this.statsTimer = null;
      this.textDecoder = new TextDecoder('utf-8');
      this.generation = 0;
      this.readyListener = null;
    }

    makeStats() {
      return {
        packets: 0,
        bytes: 0,
        frames: 0,
        dropped: 0,
        markerCorrections: 0,
        startedAt: Date.now(),
        previousPackets: 0,
        previousBytes: 0,
        previousFrames: 0
      };
    }

    emitStats() {
      const elapsed = Math.max(1, (Date.now() - this.stats.startedAt) / 1000);
      const snapshot = {
        packets: this.stats.packets,
        frames: this.stats.frames,
        dropped: this.stats.dropped,
        markerCorrections: this.stats.markerCorrections,
        fps: this.stats.frames - this.stats.previousFrames,
        packetsPerSecond: this.stats.packets - this.stats.previousPackets,
        bitrateKbps: Math.round((this.stats.bytes - this.stats.previousBytes) * 8 / 1000),
        averageFps: this.stats.frames / elapsed
      };
      this.stats.previousPackets = this.stats.packets;
      this.stats.previousBytes = this.stats.bytes;
      this.stats.previousFrames = this.stats.frames;
      this.onStats(snapshot);
    }

    configureFromStatus(status) {
      if (!this.muxer) return;
      if (status.clockRate) this.muxer.setClockRate(Number(status.clockRate));
      if (status.declaredFps) this.muxer.setFrameRate(Number(status.declaredFps));
      if (this.assembler && status.payloadType != null) {
        this.assembler.setPayloadType(Number(status.payloadType));
      }
      if (status.spropParameterSets) {
        const parts = String(status.spropParameterSets).split(',').map(value => value.trim()).filter(Boolean);
        if (parts.length >= 2) {
          try {
            this.muxer.setParameterSets(decodeBase64(parts[0]), decodeBase64(parts[1]));
          } catch (_) {}
        }
      }
    }

    handleRecord(type, payload) {
      if (type === RECORD_RTP) {
        this.stats.packets++;
        this.stats.bytes += payload.byteLength;
        if (this.assembler) this.assembler.push(payload);
        return;
      }

      const text = this.textDecoder.decode(payload);
      let message = { detail: text };
      try { message = JSON.parse(text); } catch (_) {}
      if (type === RECORD_STATUS) {
        this.configureFromStatus(message);
        this.onStatus(message);
      } else if (type === RECORD_ERROR) {
        throw new Error(message.detail || message.error || 'Falha na transmissão');
      }
    }

    async start(url) {
      this.stop(false);
      if (!('MediaSource' in global)) throw new Error('Este navegador não oferece Media Source Extensions');
      if (typeof AbortController !== 'function') throw new Error('Este navegador não oferece AbortController');

      const generation = ++this.generation;
      this.running = true;
      this.stats = this.makeStats();
      this.readyListener = () => {
        if (this.generation === generation && this.running) this.onPlaybackReady();
      };
      this.video.addEventListener('loadeddata', this.readyListener, { once: true });
      const abortController = new AbortController();
      this.abortController = abortController;
      const muxer = new FragmentedMp4Muxer(this.video, {
        onError: error => this.fail(error, generation),
        onVideoInfo: info => {
          if (this.generation === generation && this.running) this.onVideoInfo(info);
        }
      });
      this.muxer = muxer;
      this.muxChain = Promise.resolve();
      const assembler = new RtpH264Assembler(
        (nalus, timestamp) => {
          if (this.generation !== generation || !this.running) return;
          this.stats.frames++;
          this.muxChain = this.muxChain
            .then(() => {
              if (this.generation !== generation || !this.running || this.muxer !== muxer) return;
              return muxer.pushAccessUnit(nalus, timestamp);
            })
            .catch(error => this.fail(error, generation));
        },
        () => {
          if (this.generation === generation && this.running) this.stats.dropped++;
        },
        () => {
          if (this.generation === generation && this.running) this.stats.markerCorrections++;
        }
      );
      this.assembler = assembler;
      const parser = new RecordParser((type, payload) => {
        if (this.generation === generation && this.running) this.handleRecord(type, payload);
      });
      this.statsTimer = setInterval(() => {
        if (this.generation === generation && this.running) this.emitStats();
      }, 1000);
      this.onStatus({ state: 'connecting', detail: 'Conectando ao relay RTSP do ESP32' });

      try {
        const response = await fetch(url, {
          cache: 'no-store',
          signal: abortController.signal,
          headers: { Accept: 'application/vnd.camera-watchdog.rtp' }
        });
        if (this.generation !== generation || !this.running) return;
        if (!response.ok) {
          let detail = 'HTTP ' + response.status;
          try {
            const payload = await response.json();
            detail = payload.detail || payload.error || detail;
          } catch (_) {}
          throw new Error(detail);
        }
        if (!response.body || !response.body.getReader) throw new Error('O navegador não expõe leitura incremental da resposta');
        const reader = response.body.getReader();
        if (this.generation !== generation || !this.running) {
          try { await reader.cancel(); } catch (_) {}
          return;
        }
        this.reader = reader;
        while (this.generation === generation && this.running) {
          const result = await reader.read();
          if (result.done) break;
          parser.push(result.value);
        }
        if (this.generation === generation && this.running) {
          throw new Error('A câmera encerrou a transmissão');
        }
      } catch (error) {
        if (error && error.name === 'AbortError') return;
        this.fail(error, generation);
      }
    }

    fail(error, generation = this.generation) {
      if (!this.running || this.generation !== generation) return;
      const normalized = error instanceof Error ? error : new Error(String(error));
      this.onError(normalized);
      this.stop(false);
    }

    stop(notify = true) {
      const wasRunning = this.running;
      this.generation++;
      this.running = false;
      if (this.abortController) this.abortController.abort();
      this.abortController = null;
      if (this.reader) {
        try { this.reader.cancel(); } catch (_) {}
      }
      this.reader = null;
      if (this.statsTimer) clearInterval(this.statsTimer);
      this.statsTimer = null;
      if (this.readyListener) this.video.removeEventListener('loadeddata', this.readyListener);
      this.readyListener = null;
      if (this.assembler) this.assembler.flush();
      this.assembler = null;
      if (this.muxer) this.muxer.destroy();
      this.muxer = null;
      this.muxChain = Promise.resolve();
      if (notify && wasRunning) this.onStatus({ state: 'stopped', detail: 'Transmissão parada' });
    }
  }

  global.CameraWatchdogLivePlayer = CameraWatchdogLivePlayer;
  global.CameraWatchdogMp4Test = {
    FragmentedMp4Muxer,
    RtpH264Assembler,
    parseSps,
    makeInitSegment,
    START_CODE
  };
})(typeof window !== 'undefined' ? window : globalThis);
)CWJS";

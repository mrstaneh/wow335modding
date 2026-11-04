// wow335modding: the browser half of FMOD's Web Audio output (fmod_output_webaudio.cpp). FMOD mixes interleaved float
// frames into a ring in the program's shared memory; an AudioWorklet copies them to the speakers on the browser's
// audio thread and advances the read position, which FMOD uses as its playback cursor.
//
// One AudioContext serves the whole page, running at the output device's own sample rate: audio interfaces often run at
// 48, 96 or 192 kHz, and a context forced to FMOD's rate opened a second, resampled stream that some of them glitch on.
// The worklet converts from FMOD's rate itself. The game starts more than one FMOD system (the first only lists
// drivers and mixes silence); each gets its own worklet node and the context sums them.
addToLibrary({
  wowAudioStart__proxy: 'async', // AudioContext exists only on the page's main thread
  wowAudioStart: (ring, frames, channels, rate, readPos) => {
    const processor = `
      class WowAudio extends AudioWorkletProcessor {
        constructor(options) {
          super();
          const o = options.processorOptions;
          this.f32 = new Float32Array(o.buffer);
          this.i32 = new Int32Array(o.buffer);
          this.ring = o.ring >> 2;
          this.frames = o.frames;
          this.channels = o.channels;
          this.readPos = o.readPos >> 2;
          this.step = o.rate / sampleRate; // FMOD frames per output frame
          this.frac = 0;
        }
        process(inputs, outputs) {
          const out = outputs[0];
          const n = out[0].length;
          let r = Atomics.load(this.i32, this.readPos);
          for (let i = 0; i < n; i++) {
            const a = this.ring + r * this.channels;
            const b = this.ring + (r + 1 < this.frames ? r + 1 : 0) * this.channels;
            for (let c = 0; c < out.length; c++) {
              const ch = c < this.channels ? c : 0;
              out[c][i] = this.f32[a + ch] + (this.f32[b + ch] - this.f32[a + ch]) * this.frac;
            }
            this.frac += this.step;
            while (this.frac >= 1) {
              this.frac -= 1;
              if (++r >= this.frames) r = 0;
            }
          }
          Atomics.store(this.i32, this.readPos, r);
          return true;
        }
      }
      registerProcessor('wow-audio', WowAudio);`;

    (async () => {
      let output = Module.wowAudioOutput;
      if (!output) {
        const context = new AudioContext({ latencyHint: 'balanced' });
        output = Module.wowAudioOutput = {
          context,
          ready: context.audioWorklet.addModule(URL.createObjectURL(new Blob([processor], { type: 'text/javascript' }))),
        };
        // Browsers start audio only after the user interacts with the page.
        const resume = () => { if (context.state !== 'running') context.resume(); };
        for (const type of ['keydown', 'mousedown', 'pointerdown']) window.addEventListener(type, resume);
        resume();
      }
      await output.ready;
      const node = new AudioWorkletNode(output.context, 'wow-audio', {
        numberOfInputs: 0,
        outputChannelCount: [2],
        processorOptions: { buffer: HEAPU8.buffer, ring, frames, channels, readPos, rate },
      });
      node.connect(output.context.destination);
      // For the page (e.g. a volume control) and debugging.
      (Module.wowAudio ??= []).push({ context: output.context, ring, frames, channels, readPos, rate });
    })();
  },
});

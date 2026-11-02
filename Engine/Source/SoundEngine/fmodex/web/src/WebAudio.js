// wow335modding: the browser half of FMOD's Web Audio output (fmod_output_webaudio.cpp). FMOD mixes interleaved float
// frames into a ring in the program's shared memory; an AudioWorklet copies them to the speakers on the browser's
// audio thread and advances the read position, which FMOD uses as its playback cursor.
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
        }
        process(inputs, outputs) {
          const out = outputs[0];
          const n = out[0].length;
          let r = Atomics.load(this.i32, this.readPos);
          for (let i = 0; i < n; i++) {
            const base = this.ring + r * this.channels;
            for (let c = 0; c < out.length; c++) {
              out[c][i] = this.f32[base + (c < this.channels ? c : 0)];
            }
            if (++r >= this.frames) r = 0;
          }
          Atomics.store(this.i32, this.readPos, r);
          return true;
        }
      }
      registerProcessor('wow-audio', WowAudio);`;

    (async () => {
      const context = new AudioContext({ sampleRate: rate, latencyHint: 'interactive' });
      await context.audioWorklet.addModule(URL.createObjectURL(new Blob([processor], { type: 'text/javascript' })));
      const node = new AudioWorkletNode(context, 'wow-audio', {
        numberOfInputs: 0,
        outputChannelCount: [2],
        processorOptions: { buffer: HEAPU8.buffer, ring, frames, channels, readPos },
      });
      node.connect(context.destination);
      // For the page (e.g. a volume control) and debugging. The game starts two FMOD systems (one only lists drivers).
      (Module.wowAudio ??= []).push({ context, ring, frames, channels, readPos });
      // Browsers start audio only after the user interacts with the page.
      const resume = () => { if (context.state !== 'running') context.resume(); };
      for (const type of ['keydown', 'mousedown', 'pointerdown']) window.addEventListener(type, resume);
      resume();
    })();
  },
});

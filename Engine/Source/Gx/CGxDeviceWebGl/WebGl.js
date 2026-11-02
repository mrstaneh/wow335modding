// wow335modding: the WebGL2 context of CGxDeviceWebGl. The client renders on its own worker thread, whose loop never
// returns to the browser, so a canvas there would never show a frame. Instead the device draws into an OffscreenCanvas
// it owns and hands each finished frame to the page as an ImageBitmap (Module.wowPresent in the page shows it).
addToLibrary({
  $wowGl: { canvas: null },

  wowGlCreate__deps: ['$GL', '$wowGl'],
  wowGlCreate: (width, height) => {
    var canvas = new OffscreenCanvas(width, height);
    // alpha keeps the back buffer RGBA8, so copies into RGBA textures (glCopyTexImage2D) are allowed.
    var attributes = { alpha: true, depth: true, stencil: true, antialias: false, premultipliedAlpha: false, preserveDrawingBuffer: false, powerPreference: 'high-performance' };
    var ctx = canvas.getContext('webgl2', attributes);
    if (!ctx) return 0;
    wowGl.canvas = canvas;
    var handle = GL.registerContext(ctx, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: true });
    GL.makeContextCurrent(handle);
    return handle;
  },

  wowGlResize__deps: ['$wowGl'],
  wowGlResize: (width, height) => {
    wowGl.canvas.width = width;
    wowGl.canvas.height = height;
  },

  wowGlPresent__deps: ['$wowGl'],
  wowGlPresent: () => {
    var frame = wowGl.canvas.transferToImageBitmap();
    // 9 is CMD_CALL_HANDLER (emscripten's libpthread.js): the main thread runs Module[handler](...args).
    postMessage({ cmd: 9, handler: 'wowPresent', args: [frame] }, [frame]);
  },
});

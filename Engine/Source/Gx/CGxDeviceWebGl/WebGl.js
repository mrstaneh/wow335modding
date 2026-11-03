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

  wowGlPresent__deps: ['$GL', '$wowGl'],
  wowGlPresent: () => {
    // The page composites the frame over its own background, but blending leaves alpha values in the back buffer
    // (a Windows back buffer's alpha is never shown): every text glyph cell darkened what was under it, cutting into
    // the letter before. Present it opaque, touching only the alpha channel, and put the state back.
    var gl = GL.currentContext.GLctx;
    var mask = gl.getParameter(gl.COLOR_WRITEMASK);
    var clear = gl.getParameter(gl.COLOR_CLEAR_VALUE);
    var scissor = gl.isEnabled(gl.SCISSOR_TEST);
    var framebuffer = gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
    gl.disable(gl.SCISSOR_TEST);
    gl.colorMask(false, false, false, true);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.colorMask(mask[0], mask[1], mask[2], mask[3]);
    gl.clearColor(clear[0], clear[1], clear[2], clear[3]);
    if (scissor) gl.enable(gl.SCISSOR_TEST);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, framebuffer);

    var frame = wowGl.canvas.transferToImageBitmap();
    // 9 is CMD_CALL_HANDLER (emscripten's libpthread.js): the main thread runs Module[handler](...args).
    postMessage({ cmd: 9, handler: 'wowPresent', args: [frame] }, [frame]);
  },
});

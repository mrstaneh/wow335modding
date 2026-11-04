// wow335modding: the photo tool, loaded by index.html on localhost only. wowPhoto('DrownedSun') reads the shot list
// shots/DrownedSun.json and, with a GM character in the world, takes each shot: the character goes to the spot (.go,
// flying), the camera to first person at the shot's pitch (SetCameraPose, Camera_Scripts.cpp) with the interface
// hidden, and the frame lands in build/shots/<list>_<shot>.png through serve.py's /dev/save.
//
// A shot: { "name": "court", "map": 801, "x": -44, "y": 0, "z": 24, "yaw": 0, "pitch": -10, "wait": 4000 }
// x north, y west, z the eye; yaw in degrees (0 north, 90 west); pitch in degrees (negative looks down); wait in ms
// for the place to load (the first shot on a new map needs longer).
(() => {
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  // Runs a line of Lua in the client (Engine/Source/Os/Web/Input.cpp), waiting for the previous one to be taken.
  async function lua(code) {
    while (!Module.wowRunLua || !Module.wowRunLua(code)) await sleep(30);
  }
  const say = (text) => lua(`SendChatMessage(${JSON.stringify(text)}, "SAY")`);

  async function grab() {
    const source = document.getElementById('canvas');
    const copy = document.createElement('canvas');
    copy.width = source.width;
    copy.height = source.height;
    copy.getContext('2d').drawImage(source, 0, 0);
    return new Promise((resolve) => copy.toBlob(resolve, 'image/png'));
  }

  window.wowPhoto = async (list, only) => {
    const { shots } = await (await fetch(`shots/${list}.json?${Date.now()}`)).json();
    const saved = [];
    await say('.gm fly on');
    await lua('UIParent:Hide() SetCursorVisible(false)');
    try {
      for (const shot of shots) {
        if (only && !only.includes(shot.name)) continue;
        const facing = ((shot.yaw || 0) * Math.PI / 180).toFixed(3);
        await say(`.go xyz ${shot.x} ${shot.y} ${shot.z - 1.6} ${shot.map} ${facing}`);
        await sleep(shot.wait || 3500);
        await lua(`SetCameraPose(0, ${-(shot.pitch || 0)}, 0)`);
        await sleep(1200);
        const response = await fetch(`dev/save?name=${list}_${shot.name}`, { method: 'POST', body: await grab() });
        saved.push(response.ok ? await response.text() : `${shot.name}: ${await response.text()}`);
      }
    } finally {
      await lua('UIParent:Show() SetCursorVisible(true)');
    }
    return saved;
  };
})();

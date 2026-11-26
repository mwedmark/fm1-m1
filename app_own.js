// SPDX-License-Identifier: GPL-3.0-only
// M1-emulatorns egen del av sidan (efter fm1-d50 web/emu/app.js): "Load M1 data", din egen m1.fm1b (README.md) läst i den här
// webbläsaren och skriven in i emulatorns dataområde som resursregionen håller den (fm1-base device/res.c: "RES1", längd och CRC-32,
// postens egen CRC; sedan banken vid APP_RES_BASE + 32). Dataområdet sparas i webbläsarens IndexedDB (fm1-base:s "data"-del).
// Inget laddas upp: sidan har ingen Korg-data, och utan banken visar firmware NO M1 DATA.
import * as data from "./parts/data.js";

const ID = "m1";                          // fm1.toml:s name med gemener: data-delens nyckel
const RES_BASE = 0x34000;                // app.h APP_RES_BASE
const RES_BYTES = 170 * 4096;             // app.h APP_RES_SECTORS
const RES_MAGIC = 0x31534552;             // "RES1"

function crc32(u8) {                      // zlib's
  let c = ~0;
  for (let i = 0; i < u8.length; i++) {
    c ^= u8[i];
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

async function load(file, say) {
  const payload = new Uint8Array(await file.arrayBuffer());
  if (payload.length < 16 || String.fromCharCode(...payload.subarray(0, 4)) !== "FM1B") {
    say(`${file.name} is not an M1 bank (FM1B): README.md, "Från dump till bank".`);
    return;
  }
  if (32 + payload.length > RES_BYTES) {
    say(`${file.name} is ${payload.length} bytes: the region holds ${RES_BYTES - 32}.`);
    return;
  }
  const now = await data.current();
  if (!now) {
    say("Switch the emulator on first, then load the data.");
    return;
  }
  const area = new Uint8Array(now.data), at = RES_BASE - now.base;
  if (at < 0 || at + 32 + payload.length > area.length) {
    say("The emulator's data area does not hold the M1's region.");
    return;
  }
  const head = new DataView(new ArrayBuffer(32));
  head.setUint32(0, RES_MAGIC, true);
  head.setUint32(4, payload.length, true);
  head.setUint32(8, crc32(payload), true);
  head.setUint32(28, crc32(new Uint8Array(head.buffer, 0, 28)), true);
  area.set(new Uint8Array(head.buffer), at);
  area.set(payload, at + 32);
  data.stop();                            // (what the running emulator writes would replace it)
  await data.keep(ID, area.buffer);
  say(`${file.name} is kept in this browser. Starting again with it.`);
  location.reload();
}

export function attach(ctx) {
  const say = ctx.status || ((s) => console.log(s));
  const bar = document.querySelector(".bar");
  if (!bar || document.getElementById("m1-load")) return;
  const input = document.createElement("input");
  input.type = "file";
  input.accept = ".fm1b";
  input.hidden = true;
  input.id = "m1-file";
  const button = document.createElement("button");
  button.type = "button";
  button.id = "m1-load";
  button.textContent = "Load M1 data";
  button.title = "Your own m1.fm1b (README.md): read and kept in this browser, never uploaded";
  button.addEventListener("click", () => input.click());
  input.addEventListener("change", () => {
    const f = input.files && input.files[0];
    input.value = "";
    if (f) load(f, say).catch((e) => say("The data could not be loaded: " + (e && e.message || e)));
  });
  bar.append(button, input);
}

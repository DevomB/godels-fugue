// Drives the web demo in a real browser: the workflows a person uses, not the
// module alone (tests/web_smoke.mjs). It serves an assembled site, opens it in
// Chrome through playwright-core, and checks what the page shows and does.
// Usage: node tests/browser_test.mjs SITE_DIR [ARTIFACT_DIR]
// Needs: npm install playwright-core, and Chrome (the CI runners have it).
import { createServer } from "node:http";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { extname, join, resolve } from "node:path";
import { chromium } from "playwright-core";

const site = resolve(process.argv[2] || "_site");
const artifacts = resolve(process.argv[3] || "browser-artifacts");
mkdirSync(artifacts, { recursive: true });
let failures = 0;
function check(ok, what) {
  if (ok) {
    console.log("ok " + what);
  } else {
    failures++;
    console.error("FAIL " + what);
  }
}

const TYPES = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm",
                ".txt": "text/plain", ".json": "application/json", ".png": "image/png" };
const server = createServer(function (req, res) {
  const path = decodeURIComponent(new URL(req.url, "http://x").pathname);
  const file = join(site, path === "/" ? "index.html" : path);
  if (!file.startsWith(site)) { res.writeHead(403); res.end(); return; }
  try {
    const body = readFileSync(file);
    res.writeHead(200, { "content-type": TYPES[extname(file)] || "application/octet-stream" });
    res.end(body);
  } catch {
    res.writeHead(404);
    res.end();
  }
});
await new Promise((ok) => server.listen(0, "127.0.0.1", ok));
const origin = "http://127.0.0.1:" + server.address().port + "/";

const browser = await chromium.launch({ channel: "chrome", args: ["--autoplay-policy=no-user-gesture-required"] });
const context = await browser.newContext({ viewport: { width: 1280, height: 900 } });
const errors = [];
function watch(page) {
  page.on("pageerror", (e) => errors.push(String(e)));
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
}
const page = await context.newPage();
watch(page);

const badge = () => page.textContent("#badge-text");
const summary = () => page.textContent("#summary");
async function settled(timeout = 90000) {
  await page.waitForFunction(() => !/Composing|Performing|Updating|Counting/.test(document.querySelector("#badge-text").textContent),
                             null, { timeout });
  return badge();
}
/* The score page of the newest piece: after a compose the old document stays
   until the new one loads, so wait for one this test has not seen yet. */
async function scoreFrame() {
  const handle = await page.waitForSelector("#score:not([hidden])");
  const frame = await handle.contentFrame();
  await frame.waitForFunction(() => document.getElementById("mixer") && !window.__seen, null, { timeout: 90000 });
  await frame.evaluate(() => { window.__seen = true; });
  return frame;
}
/* the score page on screen now, seen or not */
async function shownFrame() {
  return (await page.waitForSelector("#score:not([hidden])")).contentFrame();
}
/* Waits for a score page to start playing; on failure records what it shows
   instead, and the test goes on. */
async function playing(frame, label) {
  /* polled from here, so nothing depends on the page's timers or frames; every
     change of the button is logged, to show what happened if it never plays */
  const seen = [];
  const started = Date.now();
  while (Date.now() - started < 90000) {
    const now = await frame.evaluate(() => {
      const play = document.querySelector("#play");
      return play.textContent + (play.disabled ? " (disabled)" : "") + " | " +
             document.querySelector("#sound-status").textContent;
    }).catch((e) => "evaluate failed: " + String(e).split("\n")[0]);
    if (!seen.length || seen[seen.length - 1].state !== now) seen.push({ at: Date.now() - started, state: now });
    if (now.startsWith("Stop")) {
      check(true, label);
      return true;
    }
    await page.waitForTimeout(500);
  }
  check(false, label + ": the button went " + seen.map((x) => x.at + " ms " + JSON.stringify(x.state)).join(", ") +
        "; errors so far: " + errors.join(" | "));
  return false;
}
async function editorText() { return page.inputValue("#config"); }
async function setEditor(text) {
  await page.evaluate(() => { document.getElementById("advanced").open = true; });
  await page.fill("#config", text);
}
async function slider(key) {
  return page.evaluate((label) => {
    const input = Array.from(document.querySelectorAll("#sliders input, #sliders select"))
      .find((i) => i.getAttribute("aria-label") === label);
    return input ? input.value : null;
  }, key);
}
async function moveSlider(label, value) {
  await page.evaluate(([l, v]) => {
    const input = Array.from(document.querySelectorAll("#sliders input, #sliders select"))
      .find((i) => i.getAttribute("aria-label") === l);
    input.value = String(v);
    input.dispatchEvent(new Event("input", { bubbles: true }));
    input.dispatchEvent(new Event("change", { bubbles: true }));
  }, [label, value]);
}
async function proof() {
  return page.evaluate(async () => {
    const a = Array.from(document.querySelectorAll("#files a")).find((x) => x.download === "proof.json");
    if (!a) return null;
    return (await fetch(a.href)).json();
  });
}
function melodyOf(p) {
  return p.variables.filter((v) => v.kind === "pitch").sort((a, b) => a.index - b.index).map((v) => v.value).join(" ");
}

process.on("unhandledRejection", (e) => { console.error(e); });
try {
// 1. The first piece: a mood preset, composed, with its values in the controls
await page.goto(origin);
check((await settled()).startsWith("Composed"), "the first piece composes: " + (await badge()) + " / " + (await summary()));
let p = await proof();
check(p && p.status === "solved" && p.form && p.form.phrases.length > 1, "the piece has a form of several phrases");
check(p && p.perform && p.perform.times.length === p.span + 1, "the piece carries its performance");
check((await slider("Tempo (quarter notes a minute)")) === p.config.tempo,
      "the tempo control shows the preset's tempo " + p.config.tempo + ", not the default");
check((await slider("Mood")) === p.config.mood, "the mood control shows the preset's mood");
let frame = await scoreFrame();
check(await frame.$$eval(".mix-row", (r) => r.length) === p.voices, "the players panel has a row per voice");
await page.screenshot({ path: join(artifacts, "1-first-piece.png"), fullPage: true });

// 2. A performance-only change keeps the notes and does not reload the score
const notesBefore = melodyOf(p);
await frame.evaluate(() => { window.__marker = 1; });
await moveSlider("Tempo (quarter notes a minute)", Number(p.config.tempo) + 10);
await settled();
p = await proof();
check(/Performed again|Files updated/.test(await badge()), "a tempo change performs the piece again: " + (await badge()));
check(melodyOf(p) === notesBefore, "the tempo change kept every note");
check(await frame.evaluate(() => window.__marker === 1), "the score page was not reloaded");
check(/\ntempo \d+/.test(await editorText()), "the change was written to the settings");

// 3. Duplicate keys: the control shows the last one, and an edit leaves one line
await setEditor("preset hymn\ntempo 60\nvoices 3\ntempo 90 # brisk\n");
await page.waitForFunction(() => document.querySelector('#sliders input[aria-label="Tempo (quarter notes a minute)"]').value === "90", null, { timeout: 30000 });
check(true, "a later duplicate key wins in the controls, as in the engine");
await moveSlider("Tempo (quarter notes a minute)", 100);
await settled();
const text = await editorText();
check((text.match(/^tempo /gm) || []).length === 1 && /^tempo 100 # brisk$/m.test(text),
      "editing a duplicated key leaves one line with its comment: " + JSON.stringify(text));

// 4. JSON settings can be shaped too
await setEditor('{ "preset": "lament", "key": "E", "tempo": 60 }');
await page.click("#compose");
check((await settled()).startsWith("Composed"), "a JSON config composes");
await moveSlider("Stepwise lines", 2);
await settled();
const json = JSON.parse(await editorText());
check(json.w_step === "2" && json.key === "E", "a control edits a JSON config and keeps its other keys");
const pieceLink = await page.$eval("#files a[download='piece.json']", (a) => a.textContent).catch(() => null);
check(pieceLink === "piece.json", "a JSON piece downloads as piece.json");

// 5. Mixed instruments from the players panel, and their ranges
frame = await scoreFrame();
await frame.selectOption(".mix-row:nth-child(2) select", "clarinet");
await settled();
p = await proof();
check(p.players[1].part === "clarinet", "voice 2 plays the clarinet chosen in the players panel");
const sounding = p.score[1].filter((n) => n.pitch > 0).map((n) => n.pitch);
check(sounding.every((x) => x >= p.players[1].low && x <= p.players[1].high),
      "every clarinet note is inside its range " + p.players[1].low + ".." + p.players[1].high);
check(/^parts .*clarinet/m.test(await editorText()) || /"parts": ".*clarinet/.test(await editorText()),
      "the instrument choice was written to the settings");

// 6. Mute persists across composing again
frame = await scoreFrame();
await frame.click('.mix-row:nth-child(1) button[aria-label="Mute voice 1"]');
await page.click("#shuffle");
await settled();
frame = await scoreFrame();
check(await frame.getAttribute('.mix-row:nth-child(1) button[aria-label="Mute voice 1"]', "aria-pressed") === "true",
      "a muted voice stays muted in the next piece");
await frame.click('.mix-row:nth-child(1) button[aria-label="Mute voice 1"]');

// 7. Undo and redo bring back composed pieces
const shuffled = melodyOf(await proof());
await page.click("#undo");
const undone = melodyOf(await proof());
check(undone !== shuffled, "undo goes back to the piece before");
await page.click("#redo");
check(melodyOf(await proof()) === shuffled, "redo comes forward again");

// 8. A failed run does not pass old files off as its own
await setEditor("voices 9\n");
await page.click("#compose");
await settled();
check(!(await badge()).startsWith("Composed"), "an invalid config is not composed: " + (await badge()));
check(await page.$$eval("#files a", (a) => a.length) === 0, "no downloads are offered for settings that made no piece");
check(await page.isVisible("#stale"), "the old score is marked as the last piece that worked");
await page.click("#undo");
check((await badge()).startsWith("Composed"), "undo returns to a piece that worked");

// 9. Stale replies: the last request wins
await setEditor("preset dance\n");
await page.click("#compose");
await setEditor("preset hymn\nkey A\n");
await page.click("#compose");
await settled();
p = await proof();
check(p.config.key === "A" && p.config.mood === "hymn", "of two quick composes, the later one is shown");

// 10. Sharing: the link reproduces the piece
const link = page.url();
const shared = melodyOf(p);
const page2 = await context.newPage();
watch(page2);
await page2.goto(link);
await page2.waitForFunction(() => /Composed/.test(document.querySelector("#badge-text").textContent), null, { timeout: 90000 });
const p2 = await page2.evaluate(async () => {
  const a = Array.from(document.querySelectorAll("#files a")).find((x) => x.download === "proof.json");
  return (await fetch(a.href)).json();
});
check(melodyOf(p2) === shared, "a shared link composes the same piece");
await page2.close();

// 11. Playback: sampled instruments load, and stop is clean
frame = await scoreFrame();
await frame.click("#play");
await playing(frame, "sampled playback starts");
const sources = await frame.getAttribute("#play", "data-sources");
check(/sampled/.test(sources || ""), "the sampled instruments load and play: " + sources);
await page.waitForTimeout(1500);
await frame.click("#play");
await page.waitForTimeout(300);
check(await frame.textContent("#play") === "Play", "stop returns to Play");

// 12. Offline: a voice that cannot load its samples plays its synth, and says so
const offline = await context.newPage();
offline.on("pageerror", (e) => errors.push(String(e))); /* its failed loads are the point */
await offline.route("**/cdn.jsdelivr.net/**", (r) => r.abort());
await offline.goto(origin);
await offline.waitForFunction(() => /Composed/.test(document.querySelector("#badge-text").textContent), null, { timeout: 90000 });
const oframe = await (await offline.waitForSelector("#score:not([hidden])")).contentFrame();
await oframe.waitForSelector("#mixer");
await oframe.click("#play");
await playing(oframe, "offline playback starts");
check(!/sampled/.test((await oframe.getAttribute("#play", "data-sources")) || "x"), "offline, every voice plays a synth");
check(/could not load/.test(await oframe.textContent("#sound-status")), "the page says the samples could not load");
await oframe.click("#play");
await offline.close();

// 13. The dance's woodwinds load; a request that never finishes is named
await setEditor("preset dance\n");
await page.click("#compose");
check((await settled()).startsWith("Composed"), "the dance round composes: " + (await badge()) + " / " + (await summary()));
const pending = new Map();
const failed = [];
page.on("request", (r) => pending.set(r.url(), Date.now()));
page.on("requestfinished", (r) => pending.delete(r.url()));
page.on("requestfailed", (r) => { pending.delete(r.url()); failed.push(r.url() + " " + (r.failure() || {}).errorText); });
frame = await scoreFrame();
await frame.click("#play");
if (!(await playing(frame, "the woodwinds load and play"))) {
  console.error("  still loading: " + Array.from(pending.keys()).join("\n    ") +
                "\n  failed: " + failed.join("\n    "));
}
if (/Stop|Loading/.test(await frame.textContent("#play"))) await frame.click("#play");
await page.waitForTimeout(500);

// 14. Looping keeps playing past the end of the piece (on the synths, so no
// network is involved)
await frame.selectOption("#sound", "synth");
await frame.check("#loop");
await page.waitForTimeout(500);
await frame.click("#play");
await playing(frame, "the loop starts");
const total = (await proof()).perform.loopTimes.at(-1);
await page.waitForTimeout(Math.min(30000, total * 1000 + 2500));
check(await frame.textContent("#play") === "Stop", "a loop is still playing after its first pass");
await frame.click("#play");
await frame.uncheck("#loop");
await frame.selectOption("#sound", "sampled");

// 14. Every control has a name
async function unnamed(target) {
  return target.evaluate(() => Array.from(document.querySelectorAll("input, select, button, textarea")).filter((el) => {
    if (el.closest("[hidden]")) return false;
    const label = el.getAttribute("aria-label") || el.textContent.trim() || (el.labels && el.labels.length && el.labels[0].textContent.trim());
    return !label;
  }).map((el) => el.outerHTML.slice(0, 80)));
}
const nameless = (await unnamed(page)).concat(await unnamed(await shownFrame()));
check(nameless.length === 0, "every control has an accessible name" + (nameless.length ? ": " + nameless.join(" | ") : ""));

await page.screenshot({ path: join(artifacts, "2-end.png"), fullPage: true });

// 16. At phone width neither page scrolls sideways (the piano roll scrolls in its own box)
await page.setViewportSize({ width: 390, height: 844 });
await page.waitForTimeout(500);
const wide = async (target) => target.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
check(await wide(page) <= 1, "the demo fits a phone's width");
check(await wide(await shownFrame()) <= 1, "the score page fits a phone's width");
await page.screenshot({ path: join(artifacts, "3-phone.png"), fullPage: true });
}
catch (e) {
  check(false, "the test stopped: " + (e && e.message ? e.message.split("\n")[0] : e));
  await page.screenshot({ path: join(artifacts, "stopped.png"), fullPage: true }).catch(() => {});
}
const real = errors.filter((e) => !/favicon/.test(e));
check(real.length === 0, "no errors in the console" + (real.length ? ":\n  " + real.join("\n  ") : ""));
writeFileSync(join(artifacts, "console.txt"), errors.join("\n"));
await browser.close();
server.close();
if (failures) {
  console.error(failures + " browser checks failed");
  process.exit(1);
}
console.log("all browser checks passed");

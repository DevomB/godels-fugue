// Smoke test for the WebAssembly build behind the web demo. It drives the
// module the way web/worker.js does (a fresh instance per run, the config in
// the in-memory file system, main() through callMain) and checks what comes
// back. Usage: node tests/web_smoke.mjs BUILD_DIR EXAMPLES_DIR
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { join, resolve } from "node:path";

const [buildDir, examplesDir] = process.argv.slice(2).map((p) => resolve(p));
const GodelsFugue = createRequire(import.meta.url)(join(buildDir, "godels-fugue.js"));
let failures = 0;

function check(ok, what) {
  if (!ok) {
    failures++;
    console.error("FAIL " + what);
  }
}

async function run(name, args) {
  const log = [];
  const mod = await GodelsFugue({ print: (s) => log.push(s), printErr: (s) => log.push(s) });
  mod.FS.mkdir("/in");
  mod.FS.mkdir("/out");
  mod.FS.writeFile("/in/piece.txt", readFileSync(join(examplesDir, name), "utf8"));
  const status = mod.callMain(["--config", "/in/piece.txt"].concat(args));
  const file = (f) => {
    try { return Buffer.from(mod.FS.readFile("/out/" + f)); } catch { return null; }
  };
  return { status, log: log.join("\n"), file };
}

const OUT = ["--out", "/out/canon.mid", "--proof", "/out/proof.txt", "--entropy", "/out/entropy.txt"];

for (const name of ["showcase.txt", "cyclic.txt", "instrument.txt"]) {
  const r = await run(name, OUT);
  check(r.status === 0, name + ": exit " + r.status + "\n" + r.log);
  check(/^melody: /m.test(r.log), name + ": no melody line");
  const html = r.file("score.html");
  check(html && html.toString().startsWith("<!DOCTYPE html>") && !html.includes("/*PIECE_DATA*/"),
        name + ": score.html missing or not filled in");
  const wav = r.file("voices.wav");
  check(wav && wav.toString("latin1", 0, 4) === "RIFF" && wav.readUInt16LE(22) === 2, name + ": voices.wav not stereo RIFF");
  check(r.file("canon.mid")?.toString("latin1", 0, 4) === "MThd", name + ": canon.mid missing");
  check(r.file("score.abc")?.toString().startsWith("X:1"), name + ": score.abc missing");
  console.log("ok " + name);
}

{
  /* an ensemble writes the E-flat alto sax in its own key, G for concert B-flat */
  const r = await run("sax_quartet.txt", OUT);
  check(r.status === 0, "sax_quartet.txt: exit " + r.status + "\n" + r.log);
  const abc = (r.file("score.abc") || "").toString();
  check(abc.includes('name="Alto Sax in Eb"') && abc.includes("\nV:2\n[K:G] "),
        "sax_quartet.txt: score.abc does not write the alto sax in G");
  console.log("ok sax_quartet.txt");
}

{
  const r = await run("unsat.txt", OUT);
  check(r.status === 1 && /^core: /m.test(r.log), "unsat.txt: expected exit 1 with a core\n" + r.log);
  check(r.file("score.html") !== null, "unsat.txt: a failed run still writes score.html");
  console.log("ok unsat.txt");
}

{
  const r = await run("mirror.txt", ["--count", "1000000", "--time-limit", "4000"]);
  check(r.status === 0 && /^count: 1730 \(exact\)$/m.test(r.log), "mirror.txt --count: " + r.log);
  console.log("ok mirror.txt --count");
}

if (failures) {
  console.error(failures + " check(s) failed");
  process.exit(1);
}
console.log("web smoke test passed");

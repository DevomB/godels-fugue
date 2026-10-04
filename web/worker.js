// Runs canon-collapse (compiled to WebAssembly) off the main thread. Each run
// gets a fresh module instance, so no state carries over between pieces; the
// compiled module is cached, so only the first run pays for compilation.
importScripts("canon-collapse.js");

const OUTPUTS = [
  "score.html", "canon.mid", "score.musicxml", "score.ly", "score.abc",
  "voices.wav", "proof.json", "report.txt", "explain.txt", "proof.txt",
];

let compiled = null;

function instantiate(imports, done) {
  const ready = compiled
    ? Promise.resolve(compiled)
    : WebAssembly.compileStreaming(fetch("canon-collapse.wasm")).then(function (m) {
        compiled = m;
        return m;
      });
  ready
    .then(function (m) { return WebAssembly.instantiate(m, imports).then(function (i) { done(i, m); }); })
    .catch(function (err) { postMessage({ error: "Could not load the solver: " + err }); });
  return {};
}

onmessage = async function (e) {
  const { id, config, args, mode } = e.data;
  const log = [];
  const started = performance.now();
  try {
    const mod = await CanonCollapse({
      print: function (s) { log.push(s); },
      printErr: function (s) { log.push(s); },
      instantiateWasm: instantiate,
    });
    mod.FS.mkdir("/in");
    mod.FS.mkdir("/out");
    const path = config.trim().startsWith("{") ? "/in/piece.json" : "/in/piece.txt";
    mod.FS.writeFile(path, config);
    if (mode === "count") {
      // Counting writes no files: report the count line and the exit status.
      const status = mod.callMain(["--config", path, "--count", "1000000", "--time-limit", "4000"]);
      postMessage({ id, mode, status, log: log.join("\n"), ms: performance.now() - started });
      return;
    }
    const status = mod.callMain([
      "--config", path,
      "--out", "/out/canon.mid",
      "--proof", "/out/proof.txt",
      "--entropy", "/out/entropy.txt",
    ].concat(args || []));
    const files = {};
    const transfer = [];
    for (const name of OUTPUTS) {
      try {
        const bytes = mod.FS.readFile("/out/" + name);
        files[name] = bytes;
        transfer.push(bytes.buffer);
      } catch (_) {
        // A run that finds no piece writes only some of the files.
      }
    }
    postMessage({ id, status, log: log.join("\n"), files, ms: performance.now() - started }, transfer);
  } catch (err) {
    postMessage({ id, error: String(err), log: log.join("\n") });
  }
};

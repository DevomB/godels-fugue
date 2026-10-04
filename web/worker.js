// Runs godels-fugue (compiled to WebAssembly) off the main thread. Each run
// gets a fresh module instance, so no state carries over between pieces; the
// compiled module is cached, so only the first run pays for compilation.
// Every reply carries the id of the request it answers, so the page can drop
// replies to requests it has given up on.
importScripts("godels-fugue.js");

const OUTPUTS = [
  "score.html", "canon.mid", "score.musicxml", "score.ly", "score.abc",
  "voices.wav", "proof.json", "report.txt", "explain.txt", "proof.txt",
];

let compiled = null;

function instantiate(imports, done) {
  const ready = compiled
    ? Promise.resolve(compiled)
    : WebAssembly.compileStreaming(fetch("godels-fugue.wasm")).then(function (m) {
        compiled = m;
        return m;
      });
  return ready.then(function (m) {
    return WebAssembly.instantiate(m, imports).then(function (i) { done(i, m); });
  });
}

onmessage = async function (e) {
  const { id, config, args, mode } = e.data || {};
  const out = [], err = [];
  const started = performance.now();
  try {
    const mod = await GodelsFugue({
      print: function (s) { out.push(s); },
      printErr: function (s) { err.push(s); },
      instantiateWasm: function (imports, done) {
        instantiate(imports, done).catch(function (why) {
          postMessage({ id, error: "Could not load the solver: " + why });
        });
        return {};
      },
    });
    mod.FS.mkdir("/in");
    mod.FS.mkdir("/out");
    // The program reads JSON by the file name, so a config that is an object
    // is written as piece.json.
    const path = String(config).trim().startsWith("{") ? "/in/piece.json" : "/in/piece.txt";
    mod.FS.writeFile(path, config);
    const reply = function (status, extra) {
      return Object.assign({ id, mode, status, out: out.join("\n"), err: err.join("\n"),
                             ms: performance.now() - started }, extra || {});
    };
    if (mode === "resolve") {
      // The settings the run would use, without composing.
      postMessage(reply(mod.callMain(["--config", path, "--resolve"])));
      return;
    }
    if (mode === "count") {
      // Counting writes no files: the count line and the exit status.
      postMessage(reply(mod.callMain(["--config", path, "--count", "1000000", "--time-limit", "4000"].concat(args || []))));
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
    postMessage(reply(status, { files }), transfer);
  } catch (why) {
    postMessage({ id, mode, error: String(why), out: out.join("\n"), err: err.join("\n") });
  }
};

import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const compiler = path.resolve(process.argv[2] ?? path.join(process.env.USERPROFILE, '.neri/bin/neri.exe'));
const native = path.resolve(process.argv[3] ?? path.join(root, 'build/native/windows-release'));
const evidence = path.join(root, 'build/windows/tests');
fs.mkdirSync(evidence, {recursive: true});
const env = {...process.env, NERI_LIBRARY_PATH: native.replaceAll('\\', '/'), PATH: native + ';' + process.env.PATH};
let count = 0;
function execute(args, status = 0, expected, exact = true, options = {}) {
  const result = spawnSync(compiler, args, {cwd: root, env, windowsHide: true, encoding: 'utf8', timeout: 60000, maxBuffer: 32 * 1024 * 1024, ...options});
  const label = String(++count).padStart(3, '0');
  fs.writeFileSync(path.join(evidence, label + '.json'), JSON.stringify({args, status: result.status, error: result.error?.message, stdout: result.stdout, stderr: result.stderr}, null, 2));
  assert.ifError(result.error);
  assert.equal(result.status, status, `${args.join(' ')}\n${result.stdout}\n${result.stderr}`);
  if (expected !== undefined) {
    if (exact) assert.equal(result.stdout, expected, args.join(' '));
    else assert.ok((result.stdout + result.stderr).includes(expected), args.join(' ') + '\n' + result.stdout + result.stderr);
  }
  return result;
}

// Reuse the project's existing language cases and their output/exit contracts.
const driver = fs.readFileSync(path.join(root, 'tooling/build.hk'), 'utf8');
const cases = [...driver.matchAll(/this\.languageCase\("([^"]+)", (\d+), "((?:\\.|[^"\\])*)", (true|false)(?:, \[([^\]]*)\])?\)/g)];
const seen = new Set();
for (const [, source, code, escaped, exact, additional] of cases) {
  if (seen.has(source)) continue;
  seen.add(source);
  const expected = JSON.parse('"' + escaped + '"');
  const sources = [source, ...[...(additional ?? '').matchAll(/"([^"]+)"/g)].map(match => match[1])];
  execute([code === '1' ? 'check' : 'run', ...sources, '--release'], Number(code), expected, exact === 'true');
  console.log('PASS ' + source);
}
for (const release of [false, true]) {
  for (const source of ['native-records', 'native-scalars']) {
    execute(['run', `tests/contracts/${source}.hk`, ...(release ? ['--release'] : [])], 0, '');
  }
  for (const name of ['i32', 'u32', 'negative', 'signed', 'float', 'nan', 'narrow', 'add', 'subtract', 'multiply', 'divide', 'zero']) {
    execute(['run', 'tests/contracts/native-scalar-range.hk', ...(release ? ['--release'] : []), '--', name], 70, 'Neri panic NRP002', false);
  }
}
execute(['run', 'tests/cli/run-arguments.hk', '--', 'two words', '$(literal)', ''], 7, '[two words]\n[$(literal)]\n[]\n');
execute(['examples/arguments.hk', '--', 'Ada', 'Grace Hopper'], 0, 'Hello, Ada!\nHello, Grace Hopper!\n');

// Exercise process quoting, UTF-8 filesystem paths, and default .exe output.
const unicodeRoot = path.join(evidence, 'proyecto español con espacios');
fs.mkdirSync(unicodeRoot, {recursive: true});
fs.writeFileSync(path.join(unicodeRoot, 'hello.hk'), 'def main(): Void\n  console::println("¡Hola!")\nend\n');
execute(['build', 'hello.hk', '--release'], 0, undefined, true, {cwd: unicodeRoot});
const hello = spawnSync(path.join(unicodeRoot, 'hello.exe'), [], {windowsHide: true, encoding: 'utf8', timeout: 10000});
assert.equal(hello.status, 0);
assert.equal(hello.stdout, '¡Hola!\n');
fs.writeFileSync(path.join(unicodeRoot, 'neri.json'), JSON.stringify({version: 2, defaultUnit: 'app', units: {app: {kind: 'executable', sources: ['hello.hk']}}}));
execute(['run', '--project', 'neri.json'], 0, '¡Hola!\n', true, {cwd: unicodeRoot});

// LSP frames are byte-counted; this detects Windows CRLF translation as well as URI handling.
const server = spawn(compiler, ['lsp'], {cwd: root, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe']});
let buffer = Buffer.alloc(0), stderr = '', messages = [];
server.stderr.on('data', data => { stderr += data; });
server.stdout.on('data', data => {
  buffer = Buffer.concat([buffer, data]);
  while (true) {
    const end = buffer.indexOf('\r\n\r\n');
    if (end < 0) return;
    const length = Number(buffer.subarray(0, end).toString().match(/Content-Length: (\d+)/i)?.[1]);
    assert.ok(Number.isSafeInteger(length));
    if (buffer.length < end + 4 + length) return;
    messages.push(JSON.parse(buffer.subarray(end + 4, end + 4 + length).toString()));
    buffer = buffer.subarray(end + 4 + length);
  }
});
function send(message) {
  const body = JSON.stringify({jsonrpc: '2.0', ...message});
  server.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
}
async function receive(predicate) {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    const index = messages.findIndex(predicate);
    if (index >= 0) return messages.splice(index, 1)[0];
    if (server.exitCode !== null) throw new Error(`LSP exited ${server.exitCode}: ${stderr}`);
    await new Promise(resolve => setTimeout(resolve, 20));
  }
  throw new Error('LSP timeout: ' + stderr + JSON.stringify(messages));
}
try {
  const uri = pathToFileURL(path.join(unicodeRoot, 'hello.hk')).href;
  send({id: 1, method: 'initialize', params: {rootUri: pathToFileURL(unicodeRoot).href, capabilities: {}}});
  assert.equal((await receive(m => m.id === 1)).result.serverInfo.name, 'neri');
  send({method: 'initialized', params: {}});
  send({method: 'textDocument/didOpen', params: {textDocument: {uri, languageId: 'neri', version: 1, text: 'def main(): Void\n  let value: Int = "wrong"\nend\n'}}});
  const diagnostics = await receive(m => m.method === 'textDocument/publishDiagnostics' && m.params.uri === uri && m.params.diagnostics.length > 0);
  assert.ok(diagnostics.params.diagnostics.some(d => d.severity === 1));
  send({method: 'textDocument/didChange', params: {textDocument: {uri, version: 2}, contentChanges: [{text: 'def main(): Void\n  let value: Int = 42\nend\n'}]}});
  await receive(m => m.method === 'textDocument/publishDiagnostics' && m.params.uri === uri && m.params.diagnostics.length === 0);
  send({id: 2, method: 'shutdown', params: null});
  assert.equal((await receive(m => m.id === 2)).result, null);
  send({method: 'exit'});
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('LSP did not exit')), 5000);
    server.once('exit', code => { clearTimeout(timeout); code === 0 ? resolve() : reject(new Error(`LSP exit ${code}`)); });
  });
} finally {
  server.kill();
  fs.writeFileSync(path.join(evidence, 'lsp.stderr'), stderr);
}
console.log(`PASS ${count} CLI cases, UTF-8 paths and LSP lifecycle/diagnostics`);

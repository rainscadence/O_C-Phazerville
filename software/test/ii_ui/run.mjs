// Compile the actual menu entry point and ii page, not a second UI model.
// Hardware drawing/queues are stubbed; USB pixel fidelity is a bench check.
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { execFileSync } from 'node:child_process';

const here = dirname(fileURLToPath(import.meta.url));
const software = resolve(here, '../..');
const source = readFileSync(join(software, 'src/OC_apps.cpp'), 'utf8');
const header = readFileSync(join(software, 'src/OC_ui.h'), 'utf8');
function body(text, name) {
  const start = text.indexOf(name);
  if (start < 0) throw new Error('Missing production function: ' + name);
  const brace = text.indexOf('{', start);
  let depth = 1, end = brace + 1;
  for (; depth && end < text.length; end++) {
    if (text[end] === '{') depth++;
    if (text[end] === '}') depth--;
  }
  if (depth) throw new Error('Unbalanced function: ' + name);
  return text.slice(start, end);
}
const iiArgs = header.match(/void IiSettings\(([^)]*)\);/);
if (!iiArgs) throw new Error('Missing production IiSettings declaration');
const build = mkdtempSync(join(tmpdir(), 'phz-ii-ui-'));
const host = readFileSync(join(here, 'Host.h'), 'utf8').replace('@II_ARGS@', iiArgs[1]);
const unit = join(build, 'menu.cpp');
writeFileSync(unit, host + '\nnamespace OC {\n' +
  body(source, 'bool Ui::AppSettings(') + '\n' +
  body(source, 'void Ui::IiSettings(') + '\n}\n' +
  readFileSync(join(here, 'cases.cpp'), 'utf8'));
const binary = join(build, 'menu-test');
execFileSync(process.env.CXX || 'clang++', [
  '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
  '-g', '-DARDUINO_TEENSY41', '-I', join(software, 'src'), unit,
  join(software, 'src/ii/AddressState.cpp'), join(software, 'src/ii/Protocol.cpp'),
  '-o', binary,
], {stdio: 'inherit'});
for (const test of ['idle-capture', 'edit-save-back', 'failure-reopen', 'bounds-and-budget', 'disabled', 'opt-in', 'tx-mode']) {
  try {
    execFileSync(binary, [test], {stdio: 'inherit', timeout: 2000});
  } catch (error) {
    if (error.code === 'ETIMEDOUT') console.error('FAIL: ' + test + ': menu blocked background/capture service for >2 seconds');
    process.exit(1);
  }
}
console.log('PASS: production menu routing/render/events; USB hardware retest still required.');

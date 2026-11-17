// terms.mjs: where the export tools find their drawn-text lists (public release Task 6), tools/terms.py's JS twin.
//
// The lists live only in private modules beside this file (tools/export/<name>.mjs, such as dw_terms and rd_terms),
// which the public export leaves out. A public tree is marked by a PUBLIC_TREE file at its root (written only by the
// export). There a missing module means "no list: only the neutral mark is refused"; anywhere else it is a failure.
// A stray marker never disables a list: a module that is present is always loaded, marker or not (and check_apps
// static fails on a checkout that holds both).
//
//   import { NEUTRAL_MARK, loadTerms } from './export/terms.mjs';
//   const m = await loadTerms(root, 'dw_terms');   // the module, or null in a public tree; throws anywhere else
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

export const NEUTRAL_MARK = 'zzneutralmarkzz';
export const MARKER = 'PUBLIC_TREE';

export const publicTree = (root) => fs.existsSync(path.join(root, MARKER));

export async function loadTerms(root, name) {
  if (!/^[a-z]+_terms$/.test(name)) throw new Error(`terms: ${name} is not a term module's name`);
  const p = path.join(root, 'tools', 'export', name + '.mjs');
  if (!fs.existsSync(p)) {
    if (publicTree(root)) return null;
    throw new Error(`tools/export/${name}.mjs is missing (and ${root} holds no ${MARKER} marker: not a public tree)`);
  }
  return import(pathToFileURL(p).href);
}

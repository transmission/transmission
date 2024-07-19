import fs from 'node:fs';

const obj = JSON.parse(fs.readFileSync('package.json', 'utf8'));

obj.scripts = (({ build }) => {
  return { build };
})(obj.scripts);

obj.devDependencies = (({
  esbuild,
  'esbuild-sass-plugin': esbuild_saas_plugin,
}) => {
  return { esbuild, 'esbuild-sass-plugin': esbuild_saas_plugin };
})(obj.devDependencies);

// the replace() call adds a trailing newline if it doesn't exist
fs.writeFileSync(
  'package.json.buildonly',
  JSON.stringify(obj, null, 2).replace(/^(.*?[^\n])$/s, '$1\n'),
);

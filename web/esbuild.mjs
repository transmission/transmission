import * as esbuild from 'esbuild';
import * as process from 'node:process';
import {sassPlugin} from 'esbuild-sass-plugin';

const ctx = await esbuild.context({
  bundle: true,
  entryNames: '[dir]/[name]',
  entryPoints: {
    'login/login': './src/login/login.js',
    'transmission-app': './src/main.js'
  },
  legalComments: 'external',
  loader: {
    '.png': 'dataurl',
    '.svg': 'dataurl' },
  minify: true,
  outdir: './public_html/',
  plugins: [sassPlugin()],
  sourcemap: true,
});

if (process.env.DEV) {
  await ctx.watch();
  console.log('watching...');
} else {
  await ctx.rebuild();
  ctx.dispose();
}


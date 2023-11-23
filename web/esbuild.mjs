import * as esbuild from 'esbuild'
import {sassPlugin} from 'esbuild-sass-plugin'

await esbuild.build({
  bundle: true,
  entryPoints: ['./src/main.js'],
  loader: {
    '.png': 'binary',
    '.svg': 'binary' },
  minify: true,
  outfile: './public_html/transmission-app.js',
  sourcemap: true,
  plugins: [sassPlugin()],
})

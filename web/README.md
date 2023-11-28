# Transmission Web Client

A web interface is built into all Transmission flavors, enabling them to be controlled remotely.

## Notes for Packagers

### Building Without Node

Transmission includes a prebuilt webapp bundle in its releases. This is
done because it's not easy to install the bundling tools on all of the
platforms that Transmission supports. Debian can't use this prebuilt
bundle due to its (understandable) policies that require building from
source. Unfortunately, building with `node run build` is also problematic
because of some of `package.json`'s `devDependencies` aren't available as
Debian packages.

Follow these steps to build the webapp from source on Debian without Node:

```sh
$ sudo apt install rsass perl esbuild
$ cd transmission/web/
$ rsass assets/css/transmission-app.scss > assets/css/transmission-app.css
$ perl -p -i -e 's/transmission-app.scss/transmission-app.css/' src/main.js
$ esbuild \
  --allow-overwrite \
  --bundle \
  --legal-comments=external \
  --loader:.png=binary \
  --loader:.svg=binary \
  --minify \
  --outfile=public_html/transmission-app.js \
  src/main.js
```

## Notes for Developers

```sh
$ npm install
$ npm run dev
```

Navigate to [localhost:9000](http://localhost:9000/) to run the app.

When you use `npm run dev`, the bundler will stay running in the
background and will rebuild transmission-app.js whenever you change
and save a source file. When it's done, you can reload the page in
your browser to see your changes in action.

## Notes for Testers

Use this bookmarklet to anonymize your torrent names before submitting a screenshot:

`javascript:void%20function(){const%20a=document.getElementsByClassName(%22torrent-name%22);for(const%20b%20of%20a)console.log(b),b.textContent=%22Lorem%20ipsum%20dolor%20sit%20amet.iso%22}();`

You’ll typically have about 3 seconds before the next batch of RPC updates overwrite the text content of any currently-downloading files.

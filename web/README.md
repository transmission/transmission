# Transmission Web Client

A web interface is built into all Transmission flavors, enabling them to be controlled remotely.

## Notes for Packagers

A `package.json.buildonly` file has been provided to reduce the number
of dependencies needed to build the web client. It removes the linting
rules used during development and leaves only what's needed to build.
To use it, overwrite `package.json` with it before the install & build
steps:

```sh
$ cp --force package.json.buildonly package.json
$ npm install
$ npm run build
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

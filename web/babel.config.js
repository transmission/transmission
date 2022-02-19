module.exports = {
  "plugins": [
    "@babel/plugin-proposal-class-properties"
  ],
  "presets": [
    [
      '@babel/preset-env',
      {
        corejs: "3.21",
        bugfixes: true,
        targets: '> 1%, not dead',
        useBuiltIns: 'entry'
      }
    ]
  ]
};

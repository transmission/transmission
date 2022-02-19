
module.exports = {
  "extends": [
    "stylelint-config-standard",
    "stylelint-config-sass-guidelines",
    "stylelint-config-prettier"
  ],
  "plugins": [
    "stylelint-scss"
  ],
  "rules": {
    "block-no-empty": true,
    "color-no-invalid-hex": true,
    "comment-no-empty": true,
    "declaration-block-no-duplicate-properties": true,
    "declaration-block-no-shorthand-property-overrides": true,
    "font-family-no-duplicate-names": true,
    "function-calc-no-unspaced-operator": true,
    "function-linear-gradient-no-nonstandard-direction": true,
    "function-no-unknown": null, // https://github.com/stylelint-scss/stylelint-scss/issues/589
    "max-nesting-depth": null,
    "media-feature-name-no-unknown": true,
    "no-descending-specificity": null,
    "no-duplicate-at-import-rules": true,
    "no-empty-source": true,
    "no-extra-semicolons": true,
    "no-invalid-double-slash-comments": true,
    "property-no-unknown": true,
    "scss/at-rule-no-unknown": true,
    "selector-max-compound-selectors": null,
    "selector-max-id": null,
    "selector-no-qualifying-type": [
      true,
      {
        "ignore": [
          "attribute"
        ]
      }
    ],
    "selector-pseudo-class-no-unknown": true,
    "selector-pseudo-element-no-unknown": true,
    "selector-type-no-unknown": true,
    "string-no-newline": true,
    "unit-no-unknown": true,
  }
};

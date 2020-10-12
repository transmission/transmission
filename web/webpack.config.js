const path = require('path');

const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const OptimizeCSSAssetsPlugin = require('optimize-css-assets-webpack-plugin');
const TerserPlugin = require('terser-webpack-plugin');
const webpack = require('webpack');

const mode = process.env.WEBPACK_MODE || 'production';

module.exports = {
  devtool: 'source-map',
  entry: './src/main.js',
  externals: {
    jquery: 'jQuery'
  },
  mode,
  module: {
    rules: [
      {
        test: /\.s(a|c)ss$/,
        use: [
          'style-loader', // create 'style' nodes from JS strings
          'css-loader', // translate css into commonjs
          'sass-loader', // compile sass into css
        ],
      },
      {
        test: /\.css$/i,
        use: [ 'style-loader', 'css-loader' ],
      },
      {
        test: /\.(png|jpe?g|)$/i,
        use: [
          'url-loader',
        ],
      },
      {
        test: /\.svg$/i,
        use: [
          'url-loader',
          'svgo-loader'
        ],
      },
    ],
  },
  optimization: {
    minimizer: [
      new TerserPlugin(),
      new OptimizeCSSAssetsPlugin({
        cssProcessorPluginOptions: {
          preset: ['default', { discardComments: { removeAll: true } }],
        }
      })
    ],
  },
  output: {
    filename: 'transmission-app.js' ,
    path: path.resolve(__dirname, 'public_html'), 
    sourceMapFilename: 'transmission-app.js.map'
  },
  plugins: [
    new MiniCssExtractPlugin({
      chunkFilename: '[id].css',
      filename: '[name].css'
    }),
    new webpack.optimize.LimitChunkCountPlugin({
      maxChunks: 1,
    }),
    new webpack.ProvidePlugin({
      $: 'jquery',
      jQuery: 'jquery',
    }),
  ],
  resolve: {
    extensions: ['.js', '.scss']
  },
};


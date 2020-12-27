const path = require('path');

const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const OptimizeCSSAssetsPlugin = require('optimize-css-assets-webpack-plugin');
const TerserPlugin = require('terser-webpack-plugin');
const webpack = require('webpack');

const mode = process.env.WEBPACK_MODE || 'production';
const devPort = process.env.DEV_PORT || 9000;
const rpcUrl = process.env.RPC_URL || 'http://localhost:9091/transmission/rpc';

const config = {
  devtool: 'source-map',
  entry: './src/main.js',
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
  ],
  resolve: {
    extensions: ['.js', '.scss']
  },
};

if (mode === 'development') {
  config.devServer = {
    compress: true,
    contentBase: path.join(__dirname, 'public_html'),
    historyApiFallback: {
      rewrites: [
        { from: '/transmission/web', to: '/' },
      ]
    },
    hot: true,
    port: devPort,
    proxy: {
      '/rpc': rpcUrl
    }
  };
}

module.exports = config;


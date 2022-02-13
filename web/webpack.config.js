const path = require('path');
const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const TerserPlugin = require('terser-webpack-plugin');


const mode = process.env.WEBPACK_MODE || 'production';
const devPort = process.env.DEV_PORT || 9000;
const rpcUrl = process.env.RPC_URL || 'http://localhost:9091/transmission/rpc';

const isProduction = mode === "production";

const cssChunkfilename = isProduction ? '[id].[fullhash].css' : '[id].css';
const cssFilename = isProduction ? '[name].[fullhash].css' : '[name].css';
const jsOutputFilename = '[name].[contenthash].js';


const config = {
  devtool: 'source-map',
  entry: './src',
  mode,

  module: {
    rules: [
      {
        test: /\.js$/,
        include: /src/,
        exclude: /(node_modules)/,
        use: 'babel-loader',
      },

      {
        exclude: /(node_modules)/,
        generator: {
          filename: 'assets/img/[hash][ext][query]',
        },
        include: /src/,
        test: /\.(jpe?g|png|gif|svg|webp)$/,
        type: 'asset/resource',
      },

      {
        test: /\.(sa|sc|c)ss$/,
        include: [/(src)/, /(node_modules)/],
        use: [
          isProduction ? MiniCssExtractPlugin.loader : 'style-loader',
          {
            loader: 'css-loader',
            options: {
              importLoaders: 1,
              sourceMap: !isProduction,
            },
          },
          'postcss-loader',
          {
            loader: 'sass-loader',
            options: {
              sourceMap: !isProduction,
            },
          }
        ],
      },

      {
        include: /src/,
        exclude: /(node_modules)/,
        test: /\.html$/i,
        use: 'html-loader',
      },
    ],
  },

  optimization: {
    chunkIds: (!isProduction) ? 'named' : 'deterministic',
    minimizer: [
      new TerserPlugin({
        parallel: true,
      }),
    ],
    moduleIds: (!isProduction) ? 'named' : 'deterministic',
    runtimeChunk: 'single',
    splitChunks: {
      cacheGroups: {
        vendor: {
          test: /[\\/]node_modules[\\/]/,
          name: 'vendors',
          chunks: 'all',
        },
      },
    },
  },

  output: {
    filename: path.join('js', jsOutputFilename),
    path: path.resolve(__dirname, 'public_html'),
  },

  plugins: [
    new CleanWebpackPlugin(),
    new MiniCssExtractPlugin({
      chunkFilename: path.join('assets', 'css', cssChunkfilename),
      filename: path.join('assets', 'css', cssFilename),
    }),
    new HtmlWebpackPlugin({
        favicon: './src/assets/img/favicon.ico',
        root: path.resolve(__dirname, './src'),
        scriptLoading: 'defer',
        template: 'src/index.html',
        title: 'Transmission Web Interface',
    }),
  ],

  resolve: {
    extensions: ['.js', '.scss']
  },
};

if (mode === 'development') {
  config.devServer = {
    allowedHosts: 'all',
    client: {
      overlay: true,
      progress: true
    },
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


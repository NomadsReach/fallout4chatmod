import esbuild from 'esbuild';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const options = {
  entryPoints: [path.join(__dirname, 'main.ts')],
  bundle: true,
  minify: true,
  target: 'es2020',
  format: 'iife',
  outfile: path.join(__dirname, 'dist', 'chat-bundle.js'),
  sourcemap: true,
  define: {
    'process.env.NODE_ENV': '"production"'
  },
  logLevel: 'info'
};

esbuild.build(options).catch(() => process.exit(1));

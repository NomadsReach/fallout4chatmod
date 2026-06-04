# FalloutChat - TypeScript Modernization

This refactoring transitions FalloutChat from a monolithic HTML file to a modular, type-safe TypeScript-based architecture.

## New File Structure

```
assets/views/
├── chat.html           # HTML structure only (no inline CSS/JS)
├── chat.css            # Extracted styles (external)
├── chat.ts             # Main application logic (TypeScript)
├── store.ts            # State management and window function registration
├── global.d.ts         # F4SE function declarations for IDE support
├── main.ts             # Entry point that imports store, then chat
└── chat.html.bak       # Backup of original monolithic file
```

## Key Benefits

✅ **Type Safety** — TypeScript catches `window.SendMessage('typo')` at compile-time  
✅ **IDE Support** — Autocomplete for F4SE functions via global.d.ts  
✅ **Separation of Concerns** — Logic, state, and types in separate files  
✅ **Pre-Runtime Validation** — Errors caught before deploying to game  

## Build Pipeline Setup

Since Ultralight doesn't have native ES module support, you need to bundle TypeScript → JavaScript.

### Option 1: esbuild (Recommended - Fast)

```bash
cd assets/views
npm install -D esbuild typescript @types/node
```

Create `esbuild.config.js`:

```javascript
const esbuild = require('esbuild');

esbuild.build({
  entryPoints: ['main.ts'],
  bundle: true,
  minify: true,
  target: 'es2020',
  outfile: 'dist/chat-bundle.js',
  define: {
    'process.env.NODE_ENV': '"production"'
  }
}).catch(() => process.exit(1));
```

Create `main.ts` (entry point):

```typescript
// CRITICAL: Import store FIRST so window functions exist before anything else
import './store';
// Then import chat logic
import './chat';
```

Build:

```bash
npx esbuild main.ts --bundle --minify --target=es2020 --outfile=dist/chat-bundle.js
```

Update `chat.html` to load the bundle:

```html
<script src="dist/chat-bundle.js"></script>
```

### Option 2: Vite (Modern, Hot Reload in Dev)

```bash
cd assets/views
npm install -D vite
```

Create `vite.config.js`:

```javascript
export default {
  build: {
    target: 'es2020',
    minify: 'terser',
    outDir: 'dist'
  }
};
```

Build:

```bash
npx vite build
```

### Option 3: Keep vanilla JavaScript (No build step)

If you don't want to set up a build pipeline, convert `.ts` files back to `.js` and ensure:
1. `store.js` is loaded FIRST
2. `chat.js` is loaded SECOND  
3. Remove all TypeScript types and imports

HTML:

```html
<script src="store.js"></script>
<script src="chat.js"></script>
```

## Implementation Checklist

- [ ] Backup original `chat.html` (already done: chat.html.bak)
- [ ] Extract CSS to external `chat.css`
- [ ] Create refactored `chat.html` (structure only)
- [ ] Set up TypeScript compilation (`tsconfig.json`)
- [ ] Configure bundler (esbuild or vite)
- [ ] Create `main.ts` entry point
- [ ] Run build: output should be `dist/chat-bundle.js`
- [ ] Update `chat.html` to load bundle
- [ ] Test in-game:
  - Chat opens/closes (F11)
  - Messages send and receive
  - Settings panel works
  - No console errors
- [ ] Update CMakeLists or build scripts to run bundler as part of plugin build

## Gradual Migration Path

If you want to migrate gradually:

1. Keep `chat.html` as-is for now
2. Add `.ts` files alongside `.js` for new features
3. Compile only new features to `new-features.js`
4. Load both old and new scripts
5. Over time, migrate more code to TypeScript

This lets you ship fixes immediately while modernizing incrementally.

## Window Function Contract

All F4SE functions MUST be registered in `store.ts` at module load time:

```typescript
window.SendMessage = function(text: string): void { ... };
```

NOT in component lifecycle hooks or event handlers. This ensures they exist when C++ calls `OnDomReadyCallback`.

## Debugging

**TypeScript Errors:**

```bash
npx tsc --noEmit  # Type check without emitting
```

**Bundle Issues:**

```bash
npx esbuild main.ts --bundle --sourcemap  # Include source maps
```

Open dev tools in Ultralight and check console for import errors.

## Performance Notes

- Bundled file should be ~50-100KB (minified)
- Ultralight loads synchronously, so keep asset size minimal
- Consider lazy-loading the settings modal after 2s (non-critical UI)

## Next Steps

1. Choose a bundler (esbuild recommended)
2. Create `tsconfig.json` with strict settings
3. Create `main.ts` entry point
4. Run first build
5. Update HTML to load the bundle
6. Test in-game
7. Update build pipeline documentation

---

**Questions?** Check:
- `global.d.ts` for F4SE function signatures
- `store.ts` for window registration patterns
- `chat.ts` for application logic structure

# ProcessEngine web UI

Angular app for ProcessEngine (Auto and Play modes). See the [root README](../README.md) and
[docs/api.md](../docs/api.md).

```bash
npm ci
npm start        # http://localhost:4200, proxies /api and /ws to http://localhost:8080 (PF_API overrides)
npm test         # Vitest unit and component tests
npm run lint     # ESLint (angular-eslint)
npm run e2e      # Playwright; starts ../build/server/pf_server and the dev server
```

`scripts/capture-docs.mjs` and `scripts/make_gif.py` regenerate the README images from a
running app.

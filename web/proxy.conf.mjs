// Dev-server proxy: /api and /ws go to the ProcessEngine server (PF_API overrides the target).
const target = process.env.PF_API ?? 'http://localhost:8080';
export default {
  '/api': { target, secure: false },
  '/ws': { target, secure: false, ws: true },
};

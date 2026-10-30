import { Routes } from '@angular/router';

export const routes: Routes = [
  { path: '', pathMatch: 'full', redirectTo: 'auto' },
  {
    path: 'auto',
    title: 'ProcessForge · Auto mode',
    loadComponent: () => import('./auto/auto-page').then((m) => m.AutoPage),
  },
  {
    path: 'play',
    title: 'ProcessForge · Play mode',
    loadComponent: () => import('./play/play-page').then((m) => m.PlayPage),
  },
  { path: '**', redirectTo: 'auto' },
];

import { Routes } from '@angular/router';

export const routes: Routes = [
  { path: '', pathMatch: 'full', redirectTo: 'factory' },
  {
    path: 'factory',
    title: 'ProcessForge · Factory',
    loadComponent: () => import('./factory/factory-page').then((m) => m.FactoryPage),
  },
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
  { path: '**', redirectTo: 'factory' },
];

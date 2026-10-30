import { Routes } from '@angular/router';

export const routes: Routes = [
  {
    path: 'factory',
    title: 'ProcessForge · Toy Factory',
    loadComponent: () => import('./factory/factory-page').then((m) => m.FactoryPage),
  },
  { path: '**', redirectTo: 'factory' },
];

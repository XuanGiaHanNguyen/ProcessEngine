import { Component, OnInit, inject, signal } from '@angular/core';
import { RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';

import { ApiService } from './api/api.service';
import { WorkspaceStore } from './state/workspace.store';

/** App shell: header with the Auto / Play mode switch and server status. */
@Component({
  selector: 'app-root',
  imports: [RouterOutlet, RouterLink, RouterLinkActive],
  templateUrl: './app.html',
  styleUrl: './app.scss',
})
export class App implements OnInit {
  private readonly api = inject(ApiService);
  private readonly store = inject(WorkspaceStore);
  protected readonly online = signal<boolean | null>(null);

  async ngOnInit(): Promise<void> {
    try {
      await this.api.health();
      this.online.set(true);
    } catch {
      this.online.set(false);
    }
    void this.store.loadCatalog();
  }
}

import { Component, OnInit, inject, signal } from '@angular/core';
import { RouterOutlet } from '@angular/router';

import { ApiService } from './api/api.service';

/** App shell: header with the server status, and the factory below it. */
@Component({
  selector: 'app-root',
  imports: [RouterOutlet],
  templateUrl: './app.html',
  styleUrl: './app.scss',
})
export class App implements OnInit {
  private readonly api = inject(ApiService);
  protected readonly online = signal<boolean | null>(null);

  async ngOnInit(): Promise<void> {
    try {
      await this.api.health();
      this.online.set(true);
    } catch {
      this.online.set(false);
    }
  }
}

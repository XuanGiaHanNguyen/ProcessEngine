import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';

import { App } from './app';

describe('App shell', () => {
  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [App],
      providers: [provideRouter([]), provideHttpClient(), provideHttpClientTesting()],
    });
  });

  it('shows the Auto / Play mode switch and the server status', async () => {
    const fixture = TestBed.createComponent(App);
    const http = TestBed.inject(HttpTestingController);
    fixture.detectChanges();
    http.expectOne('/api/health').flush({ status: 'ok' });
    await fixture.whenStable();
    http.match(() => true).forEach((r) => r.flush([]));
    await fixture.whenStable();
    const el = fixture.nativeElement as HTMLElement;
    const links = [...el.querySelectorAll('nav a')].map((a) => a.textContent?.trim());
    expect(links).toEqual(['Auto', 'Play']);
    expect(el.querySelector('.server-status')?.textContent).toContain('Server online');
  });
});

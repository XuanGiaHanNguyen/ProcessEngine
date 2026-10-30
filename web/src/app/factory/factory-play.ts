import {
  ChangeDetectionStrategy,
  Component,
  computed,
  inject,
  output,
  signal,
} from '@angular/core';
import { NgIcon, provideIcons } from '@ng-icons/core';
import {
  lucideBot,
  lucideChartColumn,
  lucideDownload,
  lucideLightbulb,
  lucideLink,
  lucideShare2,
  lucideStar,
  lucideTrophy,
  lucideUpload,
} from '@ng-icons/lucide';

import { download } from './factory-auto';
import { money } from './factory-models';
import { asDesign, FactoryStore } from './factory-store';
import { MachineTimeline } from './machine-timeline';

/**
 * Play helpers: a hint for the next best change, a score against the best known factory,
 * the machine timeline of the test being played back, and share / export / import.
 */
@Component({
  selector: 'app-factory-play',
  imports: [MachineTimeline, NgIcon],
  viewProviders: [
    provideIcons({
      lucideBot,
      lucideChartColumn,
      lucideDownload,
      lucideLightbulb,
      lucideLink,
      lucideShare2,
      lucideStar,
      lucideTrophy,
      lucideUpload,
    }),
  ],
  changeDetection: ChangeDetectionStrategy.OnPush,
  styleUrl: './factory-panels.scss',
  template: `
    <h3><ng-icon name="lucideLightbulb" /> Hint</h3>
    <p class="note">Stuck? Get the one change that would make your draft earn the most.</p>
    <button
      type="button"
      class="go"
      (click)="store.askHint()"
      [disabled]="store.hinting() || !store.check()?.valid"
    >
      @if (store.hinting()) {
        Thinking…
      } @else {
        <ng-icon name="lucideLightbulb" /> Get a hint
      }
    </button>
    @if (store.hint(); as hint) {
      @if (hint.found) {
        <div class="card" role="status">
          <p class="hint-text">{{ hint.description }}</p>
          <p class="gain">+{{ money(hint.gain) }} → {{ money(hint.profit) }}</p>
          <button type="button" class="go small" (click)="store.applyHint()">Do it</button>
        </div>
      } @else {
        <div class="card" role="status">
          <p class="hint-text">
            No single change earns more: your factory is at a peak. A bigger rebuild can still pay
            off.
          </p>
          <button type="button" class="soft" (click)="openAuto.emit()">
            See what <ng-icon name="lucideBot" /> Auto builds
          </button>
        </div>
      }
    }

    <h3><ng-icon name="lucideTrophy" /> How did you do?</h3>
    @if (score(); as s) {
      <p class="stars" [attr.aria-label]="s.stars + ' of 3 stars'">
        @for (i of [0, 1, 2]; track i) {
          <ng-icon name="lucideStar" [class.lit]="i < s.stars" />
        }
      </p>
      <div class="meter" aria-hidden="true">
        <span [style.width.%]="Math.min(100, s.pct)"></span>
      </div>
      <p class="note">
        Your best test earns <strong>{{ money(s.best) }}</strong
        >, {{ s.pct }}% of the best known {{ money(s.target) }}.
      </p>
    } @else {
      <p class="note">Test your own factory to get a score.</p>
    }

    <h3><ng-icon name="lucideChartColumn" /> Timeline</h3>
    @if (store.viewedTest(); as t) {
      <p class="note">{{ t.name }}: what each machine did, hour by hour. Click to jump.</p>
      <app-machine-timeline
        [hours]="t.result.hours"
        [labels]="machineLabels()"
        [hour]="store.hour()"
        (seek)="store.seek($event)"
      />
      <p class="legend">
        <span class="busy">■</span> working <span class="starved">■</span> waiting
        <span class="blocked">■</span> blocked
      </p>
    } @else {
      <p class="note">Play back a test to see each machine hour by hour.</p>
    }

    <h3><ng-icon name="lucideShare2" /> Share and save</h3>
    <div class="row-buttons">
      <button type="button" class="soft" (click)="copyLink()">
        <ng-icon name="lucideLink" /> Copy share link
      </button>
      <button type="button" class="soft" (click)="exportDesign()">
        <ng-icon name="lucideDownload" /> Export
      </button>
      <label class="soft file">
        <ng-icon name="lucideUpload" /> Import
        <input type="file" accept="application/json,.json" (change)="importDesign($event)" />
      </label>
    </div>
    @if (shareText(); as text) {
      <p class="note" role="status">{{ text }}</p>
    }
  `,
})
export class FactoryPlay {
  protected readonly store = inject(FactoryStore);
  protected readonly money = money;
  protected readonly Math = Math;
  readonly openAuto = output();

  protected readonly shareText = signal<string | null>(null);

  protected readonly score = computed(() => {
    const target = this.store
      .level()
      ?.reference_designs.find((r) => r.name === 'Best known')?.profit;
    const mine = this.store
      .tests()
      .filter((t) => t.kind === 'player')
      .map((t) => t.result.profit);
    if (!target || !mine.length) return null;
    const best = Math.max(...mine);
    const pct = Math.round((100 * best) / target);
    const stars = pct >= 100 ? 3 : pct >= 80 ? 2 : pct >= 50 ? 1 : 0;
    return { best, target, pct, stars };
  });

  protected readonly machineLabels = computed(() => {
    const t = this.store.viewedTest();
    const level = this.store.level();
    if (!t || !level) return [];
    return t.design.machines.map(
      (m) => `${level.machine_types.find((x) => x.id === m.type)?.name ?? m.type} ${m.slot}`,
    );
  });

  protected async copyLink(): Promise<void> {
    const link = this.store.shareLink(location.origin);
    try {
      await navigator.clipboard.writeText(link);
      this.shareText.set('Link copied. Anyone with it opens this level with your draft.');
    } catch {
      this.shareText.set(link);
    }
  }

  protected exportDesign(): void {
    const level = this.store.level();
    download(`${level?.id ?? 'factory'}-design.json`, {
      level: level?.id,
      design: this.store.draft(),
    });
  }

  protected async importDesign(e: Event): Promise<void> {
    const input = e.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file) return;
    let design;
    try {
      design = asDesign(JSON.parse(await file.text()));
    } catch {
      design = null;
    }
    if (!design) {
      this.shareText.set(`${file.name} is not a factory design.`);
      return;
    }
    this.store.backToDraft();
    this.store.replaceDraft(design);
    this.shareText.set(`Imported ${file.name} into the draft (undo to go back).`);
  }
}

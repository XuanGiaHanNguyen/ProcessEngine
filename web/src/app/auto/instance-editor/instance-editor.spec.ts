import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { TestBed } from '@angular/core/testing';

import { WorkspaceStore } from '../../state/workspace.store';
import { InstanceEditor } from './instance-editor';

describe('InstanceEditor', () => {
  let http: HttpTestingController;
  let store: WorkspaceStore;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [InstanceEditor],
      providers: [provideHttpClient(), provideHttpClientTesting()],
    });
    http = TestBed.inject(HttpTestingController);
    store = TestBed.inject(WorkspaceStore);
  });

  async function render() {
    const fixture = TestBed.createComponent(InstanceEditor);
    await fixture.whenStable();
    return { fixture, el: fixture.nativeElement as HTMLElement };
  }

  /** Answers the debounced validation request. */
  async function validate(fixture: { whenStable(): Promise<unknown> }, errors: string[]) {
    await new Promise((r) => setTimeout(r, 300));
    http
      .match('/api/instances/validate')
      .forEach((req) => req.flush({ valid: errors.length === 0, errors }));
    await new Promise((r) => setTimeout(r));
    await fixture.whenStable();
  }

  it('renders the instance tables', async () => {
    const { fixture, el } = await render();
    await validate(fixture, []);
    expect(el.querySelectorAll('tbody tr')).toHaveLength(3); // 2 resources + 1 process
    expect((el.querySelector('[data-testid=instance-name]') as HTMLInputElement).value).toBe(
      'Untitled',
    );
    expect(el.querySelector('[data-testid=instance-valid]')).not.toBeNull();
  });

  it('adds and removes resources', async () => {
    const { fixture, el } = await render();
    const add = [...el.querySelectorAll('button')].find((b) =>
      b.textContent?.includes('+ Resource'),
    )!;
    add.click();
    await fixture.whenStable();
    expect(store.instance().resources.map((r) => r.id)).toEqual(['wood', 'plank', 'r3']);
    (el.querySelector('[aria-label="Remove resource wood"]') as HTMLButtonElement).click();
    await fixture.whenStable();
    expect(store.instance().resources.map((r) => r.id)).toEqual(['plank', 'r3']);
    // Removing a resource also removes it from start inventory and recipes.
    expect(store.instance().start_inventory).toEqual({});
    expect(store.instance().processes[0].inputs).toEqual({});
  });

  it('renaming a resource updates every reference', async () => {
    const { fixture, el } = await render();
    const idInput = el.querySelector('[aria-label="Resource 1 id"]') as HTMLInputElement;
    idInput.value = 'timber';
    idInput.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    const inst = store.instance();
    expect(inst.resources[0].id).toBe('timber');
    expect(inst.start_inventory).toEqual({ timber: 4 });
    expect(inst.processes[0].inputs).toEqual({ timber: 2 });
  });

  it('edits numbers and shows the server validation errors', async () => {
    const { fixture, el } = await render();
    const workers = el.querySelector('[data-testid=workers]') as HTMLInputElement;
    workers.value = '0';
    workers.dispatchEvent(new Event('input'));
    expect(store.instance().workers).toBe(0);
    await validate(fixture, ['workers must be in [1, 10000]']);
    const errors = el.querySelector('[data-testid=instance-errors]');
    expect(errors?.textContent).toContain('workers must be in [1, 10000]');
  });

  it('imports JSON and fills optional fields', async () => {
    const { fixture, el } = await render();
    [...el.querySelectorAll('button')].find((b) => b.textContent?.includes('JSON'))!.click();
    await fixture.whenStable();
    const area = el.querySelector('#json-text') as HTMLTextAreaElement;
    area.value = JSON.stringify({
      resources: [{ id: 'a' }],
      processes: [],
      workers: 2,
      horizon: 5,
    });
    area.dispatchEvent(new Event('input'));
    [...el.querySelectorAll('button')].find((b) => b.textContent?.trim() === 'Apply')!.click();
    await fixture.whenStable();
    expect(store.instance()).toEqual({
      name: 'Imported',
      resources: [{ id: 'a', name: 'a', sell_price: 0 }],
      processes: [],
      start_inventory: {},
      workers: 2,
      horizon: 5,
      start_money: 0,
    });
  });
});

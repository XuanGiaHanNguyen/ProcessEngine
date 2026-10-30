import { asDesign, decodeDesign, encodeDesign } from './factory-store';
import { tinyLevel } from './testing';

describe('share links', () => {
  it('round-trips a design through a URL-safe string', () => {
    const design = tinyLevel().reference_designs[0].design;
    const text = encodeDesign(design);
    expect(text).toMatch(/^[A-Za-z0-9_-]+$/);
    expect(decodeDesign(text)).toEqual(design);
  });

  it('rejects text that is not a design', () => {
    expect(decodeDesign('not base64!')).toBeNull();
    expect(decodeDesign(btoa('{"hello":1}'))).toBeNull();
  });

  it('accepts a bare design or one wrapped as {level, design}', () => {
    const design = tinyLevel().reference_designs[0].design;
    expect(asDesign(design)).toEqual(design);
    expect(asDesign({ level: 'tiny', design })).toEqual(design);
    expect(asDesign({ orders: {}, machines: 'x', belts: [] })).toBeNull();
  });
});

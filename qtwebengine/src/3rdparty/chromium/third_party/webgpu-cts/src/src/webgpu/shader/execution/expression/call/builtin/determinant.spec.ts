export const description = `
Execution tests for the 'determinant' builtin function

T is AbstractFloat, f32, or f16
@const determinant(e: matCxC<T> ) -> T
Returns the determinant of e.
`;

import { makeTestGroup } from '../../../../../../common/framework/test_group.js';
import { GPUTest } from '../../../../../gpu_test.js';
import { TypeF16, TypeF32, TypeMat } from '../../../../../util/conversion.js';
import { allInputSources, run } from '../../expression.js';

import { builtin } from './builtin.js';
import { d } from './determinant.cache.js';

export const g = makeTestGroup(GPUTest);

g.test('abstract_float')
  .specURL('https://www.w3.org/TR/WGSL/#matrix-builtin-functions')
  .desc(`abstract float tests`)
  .params(u => u.combine('inputSource', allInputSources).combine('dimension', [2, 3, 4] as const))
  .unimplemented();

g.test('f32')
  .specURL('https://www.w3.org/TR/WGSL/#matrix-builtin-functions')
  .desc(`f32 tests`)
  .params(u => u.combine('inputSource', allInputSources).combine('dim', [2, 3, 4] as const))
  .fn(async t => {
    const dim = t.params.dim;
    const cases = await d.get(
      t.params.inputSource === 'const'
        ? `f32_mat${dim}x${dim}_const`
        : `f32_mat${dim}x${dim}_non_const`
    );
    await run(t, builtin('determinant'), [TypeMat(dim, dim, TypeF32)], TypeF32, t.params, cases);
  });

g.test('f16')
  .specURL('https://www.w3.org/TR/WGSL/#matrix-builtin-functions')
  .desc(`f16 tests`)
  .params(u => u.combine('inputSource', allInputSources).combine('dim', [2, 3, 4] as const))
  .beforeAllSubcases(t => {
    t.selectDeviceOrSkipTestCase('shader-f16');
  })
  .fn(async t => {
    const dim = t.params.dim;
    const cases = await d.get(
      t.params.inputSource === 'const'
        ? `f16_mat${dim}x${dim}_const`
        : `f16_mat${dim}x${dim}_non_const`
    );
    await run(t, builtin('determinant'), [TypeMat(dim, dim, TypeF16)], TypeF16, t.params, cases);
  });

import { FP } from '../../../../../util/floating_point.js';
import { makeCaseCache } from '../../case_cache.js';

// Cases: [f32|f16]_[non_]const
const cases = (['f32', 'f16'] as const)
  .flatMap(trait =>
    ([true, false] as const).map(nonConst => ({
      [`${trait}_${nonConst ? 'non_const' : 'const'}`]: () => {
        return FP[trait].generateScalarToIntervalCases(
          FP[trait].scalarRange(),
          nonConst ? 'unfiltered' : 'finite',
          FP[trait].sqrtInterval
        );
      },
    }))
  )
  .reduce((a, b) => ({ ...a, ...b }), {});

export const d = makeCaseCache('sqrt', cases);

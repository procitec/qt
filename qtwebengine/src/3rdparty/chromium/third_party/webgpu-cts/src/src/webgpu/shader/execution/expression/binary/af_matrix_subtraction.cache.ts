import { FP } from '../../../../util/floating_point.js';
import { sparseMatrixF64Range } from '../../../../util/math.js';
import { makeCaseCache } from '../case_cache.js';

// Cases: matCxR
const mat_cases = ([2, 3, 4] as const)
  .flatMap(cols =>
    ([2, 3, 4] as const).map(rows => ({
      [`mat${cols}x${rows}`]: () => {
        return FP.abstract.generateMatrixPairToMatrixCases(
          sparseMatrixF64Range(cols, rows),
          sparseMatrixF64Range(cols, rows),
          'finite',
          FP.abstract.subtractionMatrixMatrixInterval
        );
      },
    }))
  )
  .reduce((a, b) => ({ ...a, ...b }), {});

export const d = makeCaseCache('binary/af_matrix_subtraction', mat_cases);

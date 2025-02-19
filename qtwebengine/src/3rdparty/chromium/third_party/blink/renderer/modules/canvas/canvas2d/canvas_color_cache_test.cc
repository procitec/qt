// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/canvas/canvas2d/canvas_color_cache.h"

#include "base/test/metrics/histogram_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/platform/graphics/color.h"
#include "third_party/blink/renderer/platform/testing/task_environment.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

namespace blink {

TEST(CanvasColorCacheTest, Histograms) {
  test::TaskEnvironment task_environment;
  base::HistogramTester histogram_tester;
  CanvasColorCache cache(8);
  const Color red = Color(255, 0, 0);
  AtomicString string_x("x");
  AtomicString string_y("y");
  cache.SetCachedColor(string_x, red, ColorParseResult::kColor);
  for (int i = 0; i < 500; ++i) {
    cache.GetCachedColor(string_x);
  }
  for (int i = 0; i < 500; ++i) {
    cache.GetCachedColor(string_y);
  }
  histogram_tester.ExpectTotalCount(
      "Blink.Canvas.CanvasColorCache.Effectiveness", 1);
  histogram_tester.ExpectUniqueSample(
      "Blink.Canvas.CanvasColorCache.Effectiveness", 50, 1);
}
}  // namespace blink

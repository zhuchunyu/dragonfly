// Copyright 2024, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#include "server/tiering/small_bins.h"

#include <absl/strings/str_cat.h>

#include <algorithm>

#include "base/gtest.h"
#include "base/logging.h"
#include "server/tiering/disk_storage.h"

namespace dfly::tiering {

using namespace std;
using namespace std::string_literals;

TEST(SmallBins, SimpleStashRead) {
  SmallBins bins;

  // Fill single bin
  std::optional<SmallBins::FilledBin> bin;
  for (unsigned i = 0; !bin; i++)
    bin = bins.Stash(0, absl::StrCat("k", i), absl::StrCat("v", i));

  // Verify cut locations point to correct values
  auto segments = bins.ReportStashed(bin->first, DiskSegment{0, 4_KB});
  for (auto [dbid, key, location] : segments) {
    auto value = "v"s + key.substr(1);
    EXPECT_EQ(value, bin->second.substr(location.offset, location.length));
  }
}

TEST(SmallBins, SimpleDeleteAbort) {
  SmallBins bins;

  // Fill single bin
  std::optional<SmallBins::FilledBin> bin;
  unsigned i = 0;
  for (; !bin; i++)
    bin = bins.Stash(0, absl::StrCat("k", i), absl::StrCat("v", i));

  // Delete all even values
  for (unsigned j = 0; j <= i; j += 2)
    bins.Delete(0, absl::StrCat("k", j));

  auto remaining = bins.ReportStashAborted(bin->first);
  sort(remaining.begin(), remaining.end());

  // Expect all odd keys still to exist
  EXPECT_EQ(remaining.size(), i / 2);
  for (unsigned j = 1; j < i; j += 2) {
    std::pair<DbIndex, std::string> needle{0, absl::StrCat("k", j)};
    EXPECT_TRUE(binary_search(remaining.begin(), remaining.end(), needle)) << j;
  }
}

TEST(SmallBins, PartialStashDelete) {
  SmallBins bins;

  // Fill single bin
  std::optional<SmallBins::FilledBin> bin;
  unsigned i = 0;
  for (; !bin; i++)
    bin = bins.Stash(0, absl::StrCat("k", i), absl::StrCat("v", i));

  // Delete all even values
  for (unsigned j = 0; j <= i; j += 2)
    bins.Delete(0, absl::StrCat("k", j));

  auto segments = bins.ReportStashed(bin->first, DiskSegment{0, 4_KB});

  // Expect all odd keys still to exist
  EXPECT_EQ(segments.size(), i / 2);
  for (auto& [dbid, key, segment] : segments) {
    EXPECT_EQ(key, "k"s + bin->second.substr(segment.offset, segment.length).substr(1));
  }

  // Delete all stashed values
  while (!segments.empty()) {
    auto segment = std::get<2>(segments.back());
    segments.pop_back();
    auto bin = bins.Delete(segment);

    EXPECT_EQ(bin.segment.offset, 0u);
    EXPECT_EQ(bin.segment.length, 4_KB);

    if (segments.empty()) {
      EXPECT_TRUE(bin.empty);
    } else {
      EXPECT_TRUE(bin.fragmented);  // half of the values were deleted
    }
  }
}

}  // namespace dfly::tiering

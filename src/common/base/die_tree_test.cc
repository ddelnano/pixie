/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "src/common/base/base.h"
#include "src/common/base/die_tree.h"

namespace px {

TEST(DieTree, Insertion) {
  DieTree tree;
  tree.Insert(1);

  EXPECT_EQ(tree.root->interval_start, 1);
  EXPECT_EQ(tree.root->interval_end, 1);

  // Insert range that will extend an existing range
  tree.Insert(2);
  EXPECT_EQ(tree.root->interval_start, 1);
  EXPECT_EQ(tree.root->interval_end, 2);

  tree.Insert(4);
  EXPECT_EQ(tree.root->interval_start, 1);
  EXPECT_EQ(tree.root->interval_end, 2);
  EXPECT_EQ(tree.root->right->interval_start, 4);
  EXPECT_EQ(tree.root->right->interval_end, 4);

  tree.Insert(3);
  EXPECT_EQ(tree.root->interval_start, 1);
  EXPECT_EQ(tree.root->interval_end, 4);
}

}  // namespace px

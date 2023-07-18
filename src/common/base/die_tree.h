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

#pragma once

#include "src/common/base/logging.h"

namespace px {

class DieNode {
  public:
    // TODO(ddelnano): Change this to numeric template parameter
    size_t interval_start;
    size_t interval_end;
    DieNode* left;
    DieNode* right;

    DieNode() = delete;
    DieNode(size_t num) {
      LOG(WARNING) << "Calling DieNode constructor";
      interval_start = num;
      interval_end = num;
      left = nullptr;
      right = nullptr;
    };
};

class DieTree {
  public:
    DieNode* root;

    DieTree() {
      root = nullptr;
    };

    void Insert(size_t num) {
      if (root == nullptr) {
        root = new DieNode(num);
      } else {
        Insert(num, root);
      }
    }

    void Delete(size_t num) {
      PX_UNUSED(num);
    }

    std::pair<size_t, size_t> PopInterval() {
      return std::make_pair<size_t, size_t>(0, 0);
    }
  
  private:
    void JoinLeft(size_t /*num*/, DieNode* /*node*/) {
    }

    void JoinRight(size_t /*num*/, DieNode* node) {
      if (node->right == nullptr) {
        node->interval_end++;
      } else {

      }
    }

    void Insert(size_t num, DieNode* node) {
      // Match against left branch
      if (num < node->interval_start) {
        // Need to merge with the current interval
        if (num + 1 == node->interval_start) {
          JoinLeft(num, node);
        } else {
          Insert(num, node, node->left);
        }
      }

      if (num > node->interval_end) {
        // Need to merge with the current interval
        if (num == node->interval_end + 1) {
          JoinRight(num, node);
        } else if (node->right == nullptr) {
          node->right = new DieNode(num);
        } else {
          Insert(num, node, node->right);
        }
      }
    }

    void Insert(size_t num, DieNode* parent, DieNode* child) {
      PX_UNUSED(num);
      PX_UNUSED(parent);
      PX_UNUSED(child);
      if (child == nullptr) {
        /* DieNode* n = new DieNode(num); */
        /* parent->right; */
      }
    }
};


}  // namespace px

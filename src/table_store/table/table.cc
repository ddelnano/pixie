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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <absl/strings/str_format.h>
#include "internal/store_with_row_accounting.h"
#include "src/common/base/base.h"
#include "src/common/base/status.h"
#include "src/shared/types/arrow_adapter.h"
#include "src/shared/types/type_utils.h"
#include "src/table_store/schema/relation.h"
#include "src/table_store/table/internal/batch_size_accountant.h"
#include "src/table_store/table/internal/record_or_row_batch.h"
#include "src/table_store/table/internal/types.h"
#include "src/table_store/table/table.h"

// Note: this value is not used in most cases.
// Check out PL_TABLE_STORE_DATA_LIMIT_MB to configure the table store size.
DEFINE_int32(table_store_table_size_limit,
             gflags::Int32FromEnv("PL_TABLE_STORE_TABLE_SIZE_LIMIT", 1024 * 1024 * 64),
             "The maximal size a table allows. When the size grows beyond this limit, "
             "old data will be discarded.");

namespace px {
namespace table_store {

Cursor::Cursor(const Table* table, StartSpec start, StopSpec stop)
    : table_(table), hints_(internal::BatchHints{}) {
  AdvanceToStart(start);
  StopStateFromSpec(std::move(stop));
}

void Cursor::AdvanceToStart(const StartSpec& start) {
  switch (start.type) {
    case StartSpec::StartType::StartAtTime: {
      last_read_row_id_ = table_->FindRowIDFromTimeFirstGreaterThanOrEqual(start.start_time) - 1;
      break;
    }
    case StartSpec::StartType::CurrentStartOfTable: {
      if (table_->FirstRowID() == -1) {
        last_read_row_id_ = -1;
      } else {
        last_read_row_id_ = table_->FirstRowID() - 1;
      }
      break;
    }
  }
}

void Cursor::UpdateStopStateForStopAtTime() {
  if (stop_.stop_row_id_final) {
    // Once stop_row_id is set, we know the stop time is already within the table so we don't have
    // to update it anymore.
    return;
  }
  if (stop_.spec.stop_time < table_->MaxTime()) {
    stop_.stop_row_id = table_->FindRowIDFromTimeFirstGreaterThan(stop_.spec.stop_time);
    stop_.stop_row_id_final = true;
  } else {
    stop_.stop_row_id = table_->LastRowID() + 1;
  }
}

void Cursor::StopStateFromSpec(StopSpec&& stop) {
  stop_.spec = std::move(stop);
  switch (stop_.spec.type) {
    case StopSpec::StopType::CurrentEndOfTable: {
      if (table_->LastRowID() == -1) {
        stop_.stop_row_id = -1;
      } else {
        stop_.stop_row_id = table_->LastRowID() + 1;
      }
      break;
    }
    case StopSpec::StopType::StopAtTime: {
      UpdateStopStateForStopAtTime();
      break;
    }
    case StopSpec::StopType::StopAtTimeOrEndOfTable: {
      stop_.stop_row_id = table_->FindRowIDFromTimeFirstGreaterThan(stop_.spec.stop_time);
      break;
    }
    default:
      // Ignore StopType::Infinte, because it doesn't require stop_row_id.
      break;
  }
}

bool Cursor::NextBatchReady() {
  switch (stop_.spec.type) {
    case StopSpec::StopType::StopAtTimeOrEndOfTable:
    case StopSpec::StopType::CurrentEndOfTable: {
      return !Done();
    }
    case StopSpec::StopType::Infinite: {
      return last_read_row_id_ < table_->LastRowID();
    }
    case StopSpec::StopType::StopAtTime: {
      return !Done() && last_read_row_id_ < table_->LastRowID();
    }
  }
  // This return is not necessary but GCC complains without it.
  return false;
}

bool Cursor::Done() {
  auto next_row_id = last_read_row_id_ + 1;
  switch (stop_.spec.type) {
    case StopSpec::StopType::StopAtTimeOrEndOfTable:
    case StopSpec::StopType::CurrentEndOfTable: {
      return next_row_id >= stop_.stop_row_id;
    }
    case StopSpec::StopType::Infinite: {
      return false;
    }
    case StopSpec::StopType::StopAtTime: {
      UpdateStopStateForStopAtTime();
      if (!stop_.stop_row_id_final) {
        return false;
      }
      return next_row_id >= stop_.stop_row_id;
    }
  }
  // This return is not necessary but GCC complains without it.
  return false;
}

void Cursor::UpdateStopSpec(Cursor::StopSpec stop) { StopStateFromSpec(std::move(stop)); }

internal::RowID* Cursor::LastReadRowID() { return &last_read_row_id_; }

internal::BatchHints* Cursor::Hints() { return &hints_; }

std::optional<internal::RowID> Cursor::StopRowID() const {
  if (stop_.spec.type == StopSpec::StopType::Infinite) {
    return std::nullopt;
  }
  return stop_.stop_row_id;
}

StatusOr<std::unique_ptr<schema::RowBatch>> Cursor::GetNextRowBatch(
    const std::vector<int64_t>& cols) {
  return table_->GetNextRowBatch(this, cols);
}

HotColdTable::HotColdTable(std::string_view table_name, const schema::Relation& relation,
                           size_t max_table_size, size_t compacted_batch_size)
    : Table(TableMetrics(&(GetMetricsRegistry()), std::string(table_name)), relation,
            max_table_size),
      compacted_batch_size_(compacted_batch_size),
      // TODO(james): move mem_pool into constructor.
      compactor_(rel_, arrow::default_memory_pool()) {
  absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  for (const auto& [i, col_name] : Enumerate(rel_.col_names())) {
    if (col_name == "time_" && rel_.GetColumnType(i) == types::DataType::TIME64NS) {
      time_col_idx_ = i;
    }
  }
  batch_size_accountant_ = internal::BatchSizeAccountant::Create(rel_, compacted_batch_size_);
  hot_store_ = std::make_unique<internal::StoreWithRowTimeAccounting<internal::StoreType::Hot>>(
      rel_, time_col_idx_);
  cold_store_ = std::make_unique<internal::StoreWithRowTimeAccounting<internal::StoreType::Cold>>(
      rel_, time_col_idx_);
}

Status HotColdTable::ToProto(table_store::schemapb::Table* table_proto) const {
  CHECK(table_proto != nullptr);
  std::vector<int64_t> col_selector;
  for (int64_t i = 0; i < static_cast<int64_t>(rel_.NumColumns()); i++) {
    col_selector.push_back(i);
  }

  Cursor cursor(this);
  while (!cursor.Done()) {
    PX_ASSIGN_OR_RETURN(auto cur_rb, cursor.GetNextRowBatch(col_selector));
    auto eos = cursor.Done();
    cur_rb->set_eow(eos);
    cur_rb->set_eos(eos);
    PX_RETURN_IF_ERROR(cur_rb->ToProto(table_proto->add_row_batches()));
  }

  PX_RETURN_IF_ERROR(rel_.ToProto(table_proto->mutable_relation()));
  return Status::OK();
}

StatusOr<std::unique_ptr<schema::RowBatch>> HotColdTable::GetNextRowBatch(
    Cursor* cursor, const std::vector<int64_t>& cols) const {
  DCHECK(!cursor->Done()) << "Calling GetNextRowBatch on an exhausted Cursor";
  absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
  PX_ASSIGN_OR_RETURN(auto rb,
                      cold_store_->GetNextRowBatch(cursor->LastReadRowID(), cursor->Hints(),
                                                   cursor->StopRowID(), cols));
  if (rb == nullptr) {
    absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
    PX_ASSIGN_OR_RETURN(rb, hot_store_->GetNextRowBatch(cursor->LastReadRowID(), cursor->Hints(),
                                                        cursor->StopRowID(), cols));
    if (rb == nullptr && hot_store_->Size() > 0) {
      // If the cursor was pointing to an expired row batch, update the cursor to point to the start
      // of the table, then try to get the next row batch.
      *cursor->LastReadRowID() = hot_store_->FirstRowID() - 1;
      if (!cursor->Done()) {
        PX_ASSIGN_OR_RETURN(rb,
                            hot_store_->GetNextRowBatch(cursor->LastReadRowID(), cursor->Hints(),
                                                        cursor->StopRowID(), cols));
      }
    }
  }
  if (rb == nullptr) {
    return error::InvalidArgument("Data after Cursor is not in the table.");
  }
  return rb;
}

Table::RowID HotColdTable::FirstRowID() const {
  absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
  if (cold_store_->Size() > 0) {
    return cold_store_->FirstRowID();
  }
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  if (hot_store_->Size() > 0) {
    return hot_store_->FirstRowID();
  }
  return -1;
}

Table::RowID HotColdTable::LastRowID() const {
  absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  if (hot_store_->Size() > 0) {
    return hot_store_->LastRowID();
  }
  if (cold_store_->Size() > 0) {
    return cold_store_->LastRowID();
  }
  return -1;
}

Table::Time HotColdTable::MaxTime() const {
  absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  if (hot_store_->Size() > 0) {
    return hot_store_->MaxTime();
  }
  if (cold_store_->Size() > 0) {
    return cold_store_->MaxTime();
  }
  return -1;
}

Table::RowID HotColdTable::FindRowIDFromTimeFirstGreaterThanOrEqual(Time time) const {
  absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
  auto optional_row_id = cold_store_->FindRowIDFromTimeFirstGreaterThanOrEqual(time);
  if (optional_row_id.has_value()) {
    return optional_row_id.value();
  }
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  optional_row_id = hot_store_->FindRowIDFromTimeFirstGreaterThanOrEqual(time);
  if (optional_row_id.has_value()) {
    return optional_row_id.value();
  }
  return next_row_id_;
}

Table::RowID HotColdTable::FindRowIDFromTimeFirstGreaterThan(Time time) const {
  absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
  auto optional_row_id = cold_store_->FindRowIDFromTimeFirstGreaterThan(time);
  if (optional_row_id.has_value()) {
    return optional_row_id.value();
  }
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  optional_row_id = hot_store_->FindRowIDFromTimeFirstGreaterThan(time);
  if (optional_row_id.has_value()) {
    return optional_row_id.value();
  }
  return next_row_id_;
}

TableStats HotColdTable::GetTableStats() const {
  TableStats info;
  int64_t min_time = -1;
  int64_t num_batches = 0;
  int64_t hot_bytes = 0;
  int64_t cold_bytes = 0;
  {
    absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
    min_time = cold_store_->MinTime();
    num_batches += cold_store_->Size();
    absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
    num_batches += hot_store_->Size();
    hot_bytes = batch_size_accountant_->HotBytes();
    cold_bytes = batch_size_accountant_->ColdBytes();
    if (min_time == -1) {
      min_time = hot_store_->MinTime();
    }
  }
  absl::base_internal::SpinLockHolder lock(&stats_lock_);

  info.batches_added = batches_added_;
  info.batches_expired = batches_expired_;
  info.bytes_added = bytes_added_;
  info.num_batches = num_batches;
  info.bytes = hot_bytes + cold_bytes;
  info.hot_bytes = hot_bytes;
  info.cold_bytes = cold_bytes;
  info.compacted_batches = compacted_batches_;
  info.max_table_size = max_table_size_;
  info.min_time = min_time;

  return info;
}

Status HotColdTable::CompactSingleBatchUnlocked(arrow::MemoryPool*) {
  const auto& compaction_spec = batch_size_accountant_->GetNextCompactedBatchSpec();

  PX_RETURN_IF_ERROR(
      compactor_.Reserve(compaction_spec.num_rows, compaction_spec.variable_col_bytes));

  RowID first_row_id = -1;
  for (auto hot_slice : compaction_spec.hot_slices) {
    if (first_row_id == -1) {
      first_row_id = hot_store_->FirstRowID() + hot_slice.start_row;
    }

    compactor_.UnsafeAppendBatchSlice(hot_store_->front(), hot_slice.start_row, hot_slice.end_row);
    if (hot_slice.last_slice_for_batch) {
      hot_store_->PopFront();
    }
  }

  PX_ASSIGN_OR_RETURN(std::vector<ArrowArrayPtr> out_columns, compactor_.Finish());

  cold_store_->EmplaceBack(first_row_id, out_columns);

  auto num_rows_to_remove = batch_size_accountant_->FinishCompactedBatch();
  if (num_rows_to_remove > 0) {
    hot_store_->RemovePrefix(num_rows_to_remove);
  }

  {
    absl::base_internal::SpinLockHolder stat_lock(&stats_lock_);
    compacted_batches_++;
    metrics_.compacted_batches_counter.Increment();
  }
  return Status::OK();
}

Status HotColdTable::CompactHotToCold(arrow::MemoryPool* mem_pool) {
  bool next_ready = false;
  {
    absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
    next_ready = batch_size_accountant_->CompactedBatchReady();
  }
  while (next_ready) {
    absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
    absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
    // We have to check CompactedBatchReady() again, in case hot batches were expired since the last
    // check.
    if (!batch_size_accountant_->CompactedBatchReady()) {
      break;
    }
    PX_RETURN_IF_ERROR(CompactSingleBatchUnlocked(mem_pool));
    next_ready = batch_size_accountant_->CompactedBatchReady();
  }
  return Status::OK();
}

StatusOr<bool> HotColdTable::ExpireCold() {
  absl::base_internal::SpinLockHolder cold_lock(&cold_lock_);
  if (cold_store_->Size() == 0) {
    return false;
  }
  cold_store_->PopFront();
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  batch_size_accountant_->ExpireColdBatch();
  return true;
}

Status HotColdTable::ExpireBatch() {
  PX_ASSIGN_OR_RETURN(auto expired_cold, ExpireCold());
  if (expired_cold) {
    return Status::OK();
  }
  // If we get to this point then there were no cold batches to expire, so we try to expire a hot
  // batch.
  return ExpireHot();
}

Status HotColdTable::UpdateTableMetricGauges() {
  // Update table-level gauge values.
  auto stats = GetTableStats();
  // Set gauge values
  metrics_.cold_bytes_gauge.Set(stats.cold_bytes);
  metrics_.hot_bytes_gauge.Set(stats.hot_bytes);
  metrics_.num_batches_gauge.Set(stats.num_batches);
  metrics_.max_table_size_gauge.Set(stats.max_table_size);
  // Compute retention gauge
  int64_t current_retention_ns = 0;
  // If min_time is 0, there is no data in the table.
  if (stats.min_time > 0) {
    int64_t current_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
    current_retention_ns = current_time_ns - stats.min_time;
  }
  metrics_.retention_ns_gauge.Set(current_retention_ns);
  return Status::OK();
}

HotOnlyTable::HotOnlyTable(std::string_view table_name, const schema::Relation& relation,
                           size_t max_table_size)
    : Table(TableMetrics(&(GetMetricsRegistry()), std::string(table_name)), relation,
            max_table_size) {
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  for (const auto& [i, col_name] : Enumerate(rel_.col_names())) {
    if (col_name == "time_" && rel_.GetColumnType(i) == types::DataType::TIME64NS) {
      time_col_idx_ = i;
    }
  }
  batch_size_accountant_ =
      internal::BatchSizeAccountant::Create(rel_, FLAGS_table_store_table_size_limit);
  // TODO(ddelnano): Move this into the base class constructor
  hot_store_ = std::make_unique<internal::StoreWithRowTimeAccounting<internal::StoreType::Hot>>(
      rel_, time_col_idx_);
}

StatusOr<std::unique_ptr<schema::RowBatch>> HotOnlyTable::GetNextRowBatch(
    Cursor* /*cursor*/, const std::vector<int64_t>& cols) const {
  std::vector<types::DataType> col_types;
  for (int64_t col_idx : cols) {
    DCHECK(static_cast<size_t>(col_idx) < rel_.NumColumns());
    col_types.push_back(rel_.col_types()[col_idx]);
  }
  const auto row_desc = schema::RowDescriptor(col_types);
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  if (hot_store_->Size() == 0) {
    return schema::RowBatch::WithZeroRows(row_desc, /* eow */ true,
                                  /* eos */ true);
  }
  auto&& batch = hot_store_->PopFront();
  auto batch_size = batch.Length();
  auto rb = std::make_unique<schema::RowBatch>(row_desc, batch_size);
  batch_size_accountant_->ExpireHotBatch();
  PX_RETURN_IF_ERROR(hot_store_->AddBatchSliceToRowBatch(batch, 0, batch_size, cols, rb.get()));
  return rb;
}

Table::RowID HotOnlyTable::FirstRowID() const {
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  if (hot_store_->Size() > 0) {
    return hot_store_->FirstRowID();
  }
  return -1;
}

Table::RowID HotOnlyTable::LastRowID() const {
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  if (hot_store_->Size() > 0) {
    return hot_store_->LastRowID();
  }
  return -1;
}

Table::RowID HotOnlyTable::FindRowIDFromTimeFirstGreaterThanOrEqual(Time time) const {
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  auto optional_row_id = hot_store_->FindRowIDFromTimeFirstGreaterThanOrEqual(time);
  if (optional_row_id.has_value()) {
    return optional_row_id.value();
  }
  return next_row_id_;
}

Table::RowID HotOnlyTable::FindRowIDFromTimeFirstGreaterThan(Time time) const {
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  auto optional_row_id = hot_store_->FindRowIDFromTimeFirstGreaterThan(time);
  if (optional_row_id.has_value()) {
    return optional_row_id.value();
  }
  return next_row_id_;
}

Status HotOnlyTable::ToProto(table_store::schemapb::Table* table_proto) const {
  CHECK(table_proto != nullptr);
  std::vector<int64_t> col_selector;
  for (int64_t i = 0; i < static_cast<int64_t>(rel_.NumColumns()); i++) {
    col_selector.push_back(i);
  }

  Cursor cursor(this);
  while (!cursor.Done()) {
    PX_ASSIGN_OR_RETURN(auto cur_rb, cursor.GetNextRowBatch(col_selector));
    auto eos = cursor.Done();
    cur_rb->set_eow(eos);
    cur_rb->set_eos(eos);
    PX_RETURN_IF_ERROR(cur_rb->ToProto(table_proto->add_row_batches()));
  }

  PX_RETURN_IF_ERROR(rel_.ToProto(table_proto->mutable_relation()));
  return Status::OK();
}

TableStats HotOnlyTable::GetTableStats() const {
  TableStats info;
  int64_t min_time = -1;
  int64_t num_batches = 0;
  int64_t hot_bytes = 0;
  int64_t cold_bytes = 0;
  {
    absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
    num_batches += hot_store_->Size();
    hot_bytes = batch_size_accountant_->HotBytes();
    if (min_time == -1) {
      min_time = hot_store_->MinTime();
    }
  }
  absl::base_internal::SpinLockHolder lock(&stats_lock_);

  info.batches_added = batches_added_;
  info.batches_expired = batches_expired_;
  info.bytes_added = bytes_added_;
  info.num_batches = num_batches;
  info.bytes = hot_bytes + cold_bytes;
  info.hot_bytes = hot_bytes;
  info.cold_bytes = cold_bytes;
  info.compacted_batches = compacted_batches_;
  info.max_table_size = max_table_size_;
  info.min_time = min_time;

  return info;
}

Status HotOnlyTable::CompactHotToCold(arrow::MemoryPool* /*mem_pool*/) {
  LOG(INFO) << "Skipping compaction for HotOnlyTable";
  return Status::OK();
}

Table::Time HotOnlyTable::MaxTime() const {
  absl::base_internal::SpinLockHolder hot_lock(&hot_lock_);
  if (hot_store_->Size() > 0) {
    return hot_store_->MaxTime();
  }
  return -1;
}

Status HotOnlyTable::ExpireBatch() { return ExpireHot(); }

Status HotOnlyTable::UpdateTableMetricGauges() {
  // Update table-level gauge values.
  auto stats = GetTableStats();
  // Set gauge values
  metrics_.hot_bytes_gauge.Set(stats.hot_bytes);
  metrics_.num_batches_gauge.Set(stats.num_batches);
  metrics_.max_table_size_gauge.Set(stats.max_table_size);
  // Compute retention gauge
  int64_t current_retention_ns = 0;
  // If min_time is 0, there is no data in the table.
  if (stats.min_time > 0) {
    int64_t current_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
    current_retention_ns = current_time_ns - stats.min_time;
  }
  metrics_.retention_ns_gauge.Set(current_retention_ns);
  return Status::OK();
}

}  // namespace table_store
}  // namespace px

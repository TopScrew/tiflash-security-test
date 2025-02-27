// Copyright 2023 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <Common/Logger.h>
#include <Flash/Mpp/MPPTaskId.h>
#include <Flash/Mpp/TaskStatus.h>
#include <Flash/Statistics/ExecutorStatisticsCollector.h>
#include <common/StringRef.h>
#include <common/types.h>
#include <tipb/executor.pb.h>
#include <tipb/select.pb.h>

#include <chrono>
#include <map>

namespace DB
{
class MPPTaskStatistics
{
public:
    using Clock = std::chrono::system_clock;
    using Timestamp = Clock::time_point;

    MPPTaskStatistics(const MPPTaskId & id_, String address_);

    void start();

    void end(const TaskStatus & status_, StringRef error_message_ = "");

    void recordReadWaitIndex(DAGContext & dag_context);

    void initializeExecutorDAG(DAGContext * dag_context);

    /// return exchange sender runtime statistics
    const BaseRuntimeStatistics & collectRuntimeStatistics();

    void logTracingJson();

    void setMemoryPeak(Int64 memory_peak);

    void setCompileTimestamp(const Timestamp & start_timestamp, const Timestamp & end_timestamp);

private:
    void recordInputBytes(DAGContext & dag_context);

    const LoggerPtr logger;

    // common
    const MPPTaskId id;
    const String host;
    Timestamp task_init_timestamp{Clock::duration::zero()};
    Timestamp task_start_timestamp{Clock::duration::zero()};
    Timestamp task_end_timestamp{Clock::duration::zero()};
    Timestamp compile_start_timestamp{Clock::duration::zero()};
    Timestamp compile_end_timestamp{Clock::duration::zero()};
    Timestamp read_wait_index_start_timestamp{Clock::duration::zero()};
    Timestamp read_wait_index_end_timestamp{Clock::duration::zero()};
    TaskStatus status;
    String error_message;

    Int64 local_input_bytes = 0;
    Int64 remote_input_bytes = 0;
    Int64 output_bytes = 0;

    // executor dag
    bool is_root = false;
    String sender_executor_id;
    ExecutorStatisticsCollector executor_statistics_collector;

    // resource
    Int64 working_time = 0;
    Int64 memory_peak = 0;
};
} // namespace DB
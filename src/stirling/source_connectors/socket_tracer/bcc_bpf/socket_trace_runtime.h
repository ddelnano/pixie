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

static __inline void set_conn_as_ssl(uint32_t tgid, int32_t fd);

#if ACCESS_TLS_SK_FD_VIA_ACTIVE_SYSCALL

// Maps used to verify if SSL_write or SSL_read are on the stack during a syscall and
// pass the fds back to the SSL_write and SSL_read return probes
BPF_HASH(ssl_userspace_call_map, uint64_t, bool);
BPF_HASH(ssl_fd_map, uint64_t, int);

static __inline bool active_ssl_userspace_call(uint64_t tgid) {
  return ssl_userspace_call_map.lookup(&tgid) != NULL;
}

static __inline void propagate_fd_to_userspace_call(uint64_t pid_tgid, int fd) {
  if (active_ssl_userspace_call(pid_tgid)) {
    bpf_trace_printk("Setting socket fd (%d) for pid tgid (%d)", fd, pid_tgid);
    ssl_fd_map.update(&pid_tgid, &fd);

    uint32_t tgid = pid_tgid >> 32;
    set_conn_as_ssl(tgid, fd);
  }
}

#else

static __inline void propagate_fd_to_userspace_call(uint64_t pid_tgid, int fd) { }

#endif

#ifdef GET_CGROUP_ID_ENABLED
uint64_t pl_bpf_get_current_cgroup_id() {
    return bpf_get_current_cgroup_id();
}
#else

uint64_t pl_bpf_get_current_cgroup_id() {
    // TODO(ddelnano): UINT64_MAX doesn't work in BPF. Find another way to represent this.
    return (uint64_t)18446744073709551615;
}
#endif

/* Requests an OS-simulated TDR on the guest's virtio-gpu adapter. This never
 * submits an intentionally non-terminating workload to the host GPU. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <io.h>

/* Published d3dkmthk.h ABI subset. llvm-mingw does not ship that SDK header.
 * https://learn.microsoft.com/windows-hardware/drivers/ddi/d3dkmthk/ */
typedef struct {
    UINT handle;
    LUID luid;
    ULONG sources;
    BOOL precise_present_regions;
} adapter_info;

typedef struct {
    ULONG count;
    adapter_info *adapters;
} enum_adapters;

typedef struct {
    UINT handle;
    UINT type;
    void *data;
    UINT size;
} query_adapter;

typedef struct {
    UINT physical_index;
    UINT vendor_id;
    UINT device_id;
    UINT subvendor_id;
    UINT subsystem_id;
    UINT revision_id;
    UINT bus_type;
} query_device_ids;

typedef struct {
    UINT adapter;
    UINT device;
    UINT type;
    UINT flags;
    void *data;
    UINT size;
    UINT context;
} escape_args;

typedef struct { UINT handle; } close_adapter;
typedef LONG (WINAPI *enum_fn)(const enum_adapters *);
typedef LONG (WINAPI *query_fn)(const query_adapter *);
typedef LONG (WINAPI *escape_fn)(const escape_args *);
typedef LONG (WINAPI *close_fn)(const close_adapter *);

#define TDR_STATUS_MAGIC 0x56475444u
#define TDR_STATUS_VERSION 1u
typedef struct {
    uint32_t magic, version, size, reserved;
    uint64_t reset_count, restart_count;
    uint32_t last_reset_status, last_restart_status;
    uint64_t last_submitted_fence, last_completed_fence, pending_preemption_fence;
    uint32_t recovery_state, reserved2;
} tdr_status;

#define TDR_TRACE_MAGIC 0x56475458u
#define TDR_TRACE_RECORDS 256u
typedef struct {
    uint64_t sequence, tick;
    uint32_t generation, event, node, engine, context, flags;
    uint64_t fence;
    uint32_t response, pending_preemption;
} tdr_trace_record;

typedef struct {
    uint32_t magic, version, size, count;
    uint64_t last_sequence;
    tdr_trace_record records[TDR_TRACE_RECORDS];
} tdr_trace;

_Static_assert(sizeof(adapter_info) == 20, "D3DKMT_ADAPTERINFO ABI");
_Static_assert(sizeof(query_device_ids) == 28, "D3DKMT_QUERY_DEVICE_IDS ABI");
_Static_assert(offsetof(escape_args, data) == 16, "D3DKMT_ESCAPE ABI");
_Static_assert(sizeof(escape_args) == (sizeof(void *) == 8 ? 32 : 28), "D3DKMT_ESCAPE size");
_Static_assert(sizeof(tdr_status) == 72, "VGTD status ABI");
_Static_assert(offsetof(tdr_status, reset_count) == 16, "VGTD counter offset");
_Static_assert(offsetof(tdr_status, recovery_state) == 64, "VGTD state offset");
_Static_assert(sizeof(tdr_trace_record) == 56, "VGTX record ABI");
_Static_assert(offsetof(tdr_trace_record, fence) == 40, "VGTX fence offset");
_Static_assert(offsetof(tdr_trace, records) == 24, "VGTX header ABI");
_Static_assert(sizeof(tdr_trace) == 14360, "VGTX status ABI");

enum {
    QUERY_PHYSICAL_ADAPTER_DEVICE_IDS = 31,
    ESCAPE_DRIVERPRIVATE = 0,
    ESCAPE_TDRDBGCTRL = 2,
    TDR_FORCE = 0,
};

typedef struct {
    const char *result, *stage;
    LONG last_status, escape_status, telemetry_status, trace_status;
    DWORD win32_error, restore_status;
    int selected, request_completed, before_valid, after_valid, recovered;
    adapter_info adapter;
    query_device_ids ids;
    tdr_status before, after;
    tdr_trace *trace;
    int trace_valid;
    ULONGLONG started;
} result;

static void status_number(FILE *f, int valid, uint64_t value)
{
    if (valid) fprintf(f, "%llu", (unsigned long long)value);
    else fputs("null", f);
}

static void emit(FILE *f, const result *r)
{
    fprintf(f, "{\"schema\":1,\"probe\":\"tdr\",\"result\":\"%s\",\"stage\":\"%s\","
            "\"request_completed\":%s,\"escape_status\":", r->result, r->stage,
            r->request_completed ? "true" : "false");
    if (r->request_completed) fprintf(f, "\"0x%08lx\"", (unsigned long)r->escape_status);
    else fputs("null", f);
    fprintf(f, ",\"last_ntstatus\":\"0x%08lx\",\"telemetry_status\":\"0x%08lx\","
            "\"win32_error\":%lu,\"test_mode_restore_status\":%lu,"
            "\"recovery_observed\":%s,\"elapsed_ms\":%llu,\"adapter\":",
            (unsigned long)r->last_status, (unsigned long)r->telemetry_status,
            (unsigned long)r->win32_error, (unsigned long)r->restore_status,
            r->recovered ? "true" : "false", (unsigned long long)(GetTickCount64() - r->started));
    if (r->selected) {
        fprintf(f, "{\"vendor_id\":\"0x%04x\",\"device_id\":\"0x%04x\","
                "\"subvendor_id\":\"0x%04x\",\"subsystem_id\":\"0x%04x\","
                "\"revision_id\":%u,\"luid_high\":%ld,\"luid_low\":%lu}",
                r->ids.vendor_id, r->ids.device_id, r->ids.subvendor_id,
                r->ids.subsystem_id, r->ids.revision_id, (long)r->adapter.luid.HighPart,
                (unsigned long)r->adapter.luid.LowPart);
    } else fputs("null", f);
    fputs(",\"reset_before\":", f); status_number(f, r->before_valid, r->before.reset_count);
    fputs(",\"reset_after\":", f); status_number(f, r->after_valid, r->after.reset_count);
    fputs(",\"restart_before\":", f); status_number(f, r->before_valid, r->before.restart_count);
    fputs(",\"restart_after\":", f); status_number(f, r->after_valid, r->after.restart_count);
    fputs(",\"last_reset_status\":", f);
    if (r->after_valid) fprintf(f, "\"0x%08x\"", r->after.last_reset_status); else fputs("null", f);
    fputs(",\"last_restart_status\":", f);
    if (r->after_valid) fprintf(f, "\"0x%08x\"", r->after.last_restart_status); else fputs("null", f);
    fputs(",\"recovery_state\":", f); status_number(f, r->after_valid, r->after.recovery_state);
    fputs(",\"last_submitted_fence\":", f); status_number(f, r->after_valid, r->after.last_submitted_fence);
    fputs(",\"last_completed_fence\":", f); status_number(f, r->after_valid, r->after.last_completed_fence);
    fputs(",\"pending_preemption_fence\":", f); status_number(f, r->after_valid, r->after.pending_preemption_fence);
    fprintf(f, ",\"trace_status\":\"0x%08lx\",\"trace_count\":", (unsigned long)r->trace_status);
    status_number(f, r->trace_valid, r->trace_valid ? r->trace->count : 0);
    fputs(",\"trace_last_sequence\":", f);
    status_number(f, r->trace_valid, r->trace_valid ? r->trace->last_sequence : 0);
    fputs(",\"trace_records\":", f);
    if (r->trace_valid) {
        fputc('[', f);
        int separator = 0;
        for (UINT i = 0; i < TDR_TRACE_RECORDS; ++i) {
            const tdr_trace_record *record = &r->trace->records[i];
            if (!record->sequence) continue;
            if (separator++) fputc(',', f);
            fprintf(f, "{\"sequence\":%llu,\"tick\":%llu,\"generation\":%u,\"event\":%u,"
                    "\"node\":%u,\"engine\":%u,\"context\":%u,\"flags\":%u,\"fence\":%llu,"
                    "\"response\":%u,\"pending_preemption\":%u}",
                    (unsigned long long)record->sequence, (unsigned long long)record->tick,
                    record->generation, record->event, record->node, record->engine,
                    record->context, record->flags, (unsigned long long)record->fence,
                    record->response, record->pending_preemption);
        }
        fputc(']', f);
    } else fputs("null", f);
    fputs("}\n", f);
}

static int save(const wchar_t *path, const result *r)
{
    if (!path) return 1;
    FILE *f = _wfopen(path, L"wb");
    if (!f) return 0;
    emit(f, r);
    int ok = fflush(f) == 0 && _commit(_fileno(f)) == 0;
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static LONG read_status(escape_fn escape, UINT adapter, tdr_status *status)
{
    memset(status, 0, sizeof(*status));
    status->magic = TDR_STATUS_MAGIC;
    status->version = TDR_STATUS_VERSION;
    status->size = sizeof(*status);
    escape_args e = {0};
    e.adapter = adapter;
    e.type = ESCAPE_DRIVERPRIVATE;
    e.data = status;
    e.size = sizeof(*status);
    LONG value = escape(&e);
    if (value >= 0 && (status->magic != TDR_STATUS_MAGIC || status->version != TDR_STATUS_VERSION ||
                       status->size != sizeof(*status))) return (LONG)0xc000000du;
    return value;
}

static LONG read_trace(escape_fn escape, UINT adapter, tdr_trace *trace)
{
    trace->magic = TDR_TRACE_MAGIC;
    trace->version = TDR_STATUS_VERSION;
    trace->size = sizeof(*trace);
    escape_args e = {0};
    e.adapter = adapter;
    e.type = ESCAPE_DRIVERPRIVATE;
    e.data = trace;
    e.size = sizeof(*trace);
    LONG value = escape(&e);
    if (value >= 0 && (trace->magic != TDR_TRACE_MAGIC || trace->version != TDR_STATUS_VERSION ||
                       trace->size != sizeof(*trace) || trace->count > TDR_TRACE_RECORDS)) return (LONG)0xc000000du;
    return value;
}

int wmain(int argc, wchar_t **argv)
{
    result r = {0};
    r.result = "api_failure";
    r.stage = "arguments";
    r.started = GetTickCount64();
    r.telemetry_status = (LONG)0xc00000bbu;
    r.trace_status = (LONG)0xc00000bbu;
    const wchar_t *out = NULL;
    int force = 0, inspect = 0, trace = 0, exit_code = 1;
    DWORD wait_ms = 30000;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--force-tdr")) force = 1;
        else if (!wcscmp(argv[i], L"--status")) inspect = 1;
        else if (!wcscmp(argv[i], L"--trace")) trace = 1;
        else if (!wcscmp(argv[i], L"--out") && i + 1 < argc) out = argv[++i];
        else if (!wcscmp(argv[i], L"--wait-ms") && i + 1 < argc) {
            wchar_t *end;
            unsigned long value = wcstoul(argv[++i], &end, 10);
            if (*end || value < 100 || value > 120000) { r.result = "usage"; exit_code = 3; goto done; }
            wait_ms = (DWORD)value;
        } else {
            fputs("usage: tdr_probe.exe (--force-tdr | --status | --trace) [--out FILE] [--wait-ms 100..120000]\n", stderr);
            r.result = "usage"; exit_code = 3; goto done;
        }
    }
    if (force + inspect + trace != 1) { r.result = "usage"; exit_code = 3; goto done; }

    HMODULE gdi = LoadLibraryExW(L"gdi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    r.stage = "load_gdi32";
    if (!gdi) { r.win32_error = GetLastError(); goto done; }
    enum_fn enumerate = (enum_fn)GetProcAddress(gdi, "D3DKMTEnumAdapters2");
    query_fn query = (query_fn)GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
    escape_fn escape = (escape_fn)GetProcAddress(gdi, "D3DKMTEscape");
    close_fn close = (close_fn)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    if (!enumerate || !query || !escape || !close) { r.win32_error = ERROR_PROC_NOT_FOUND; FreeLibrary(gdi); goto done; }
    enum_adapters adapters = {0};
    r.stage = "enumerate_count";
    r.last_status = enumerate(&adapters);
    if (r.last_status < 0 || !adapters.count || adapters.count > 1024) { FreeLibrary(gdi); goto done; }
    adapters.adapters = calloc(adapters.count, sizeof(*adapters.adapters));
    if (!adapters.adapters) { r.win32_error = ERROR_OUTOFMEMORY; FreeLibrary(gdi); goto done; }
    r.stage = "enumerate_adapters";
    r.last_status = enumerate(&adapters);
    if (r.last_status < 0) { free(adapters.adapters); FreeLibrary(gdi); goto done; }
    UINT matches = 0;
    for (ULONG i = 0; i < adapters.count; ++i) {
        query_device_ids ids = {0};
        query_adapter q = {adapters.adapters[i].handle, QUERY_PHYSICAL_ADAPTER_DEVICE_IDS, &ids, sizeof(ids)};
        LONG value = query(&q);
        if (value >= 0 && ids.vendor_id == 0x1af4 && ids.device_id == 0x1050) {
            ++matches;
            r.adapter = adapters.adapters[i];
            r.ids = ids;
            r.selected = 1;
        }
    }
    r.stage = "select_virtio_gpu";
    if (matches != 1) { r.result = "adapter_selection_failed"; goto close_adapters; }
    if (trace) {
        r.stage = "read_trace";
        r.trace = calloc(1, sizeof(*r.trace));
        if (!r.trace) { r.win32_error = ERROR_OUTOFMEMORY; goto close_adapters; }
        r.trace_status = read_trace(escape, r.adapter.handle, r.trace);
        r.last_status = r.trace_status;
        r.trace_valid = r.trace_status >= 0;
        if (r.trace_valid) { r.result = "trace"; exit_code = 0; }
        goto close_adapters;
    }
    r.stage = "read_status_before";
    r.telemetry_status = read_status(escape, r.adapter.handle, &r.before);
    r.before_valid = r.telemetry_status >= 0;
    if (inspect) {
        r.after = r.before;
        r.after_valid = r.before_valid;
        if (r.before_valid) { r.result = "status"; exit_code = 0; }
        goto close_adapters;
    }

    /* This OS test escape is disabled unless TdrTestMode is one. Preserve
     * the exact prior DWORD/absence and restore it after the request. */
    HKEY key;
    DWORD old_mode = 0, old_type = 0, old_size = sizeof(old_mode);
    int old_present = 0;
    r.stage = "open_test_mode";
    r.win32_error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
                                  0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
    if (r.win32_error) goto close_adapters;
    r.win32_error = RegQueryValueExW(key, L"TdrTestMode", NULL, &old_type, (BYTE *)&old_mode, &old_size);
    if (r.win32_error == ERROR_SUCCESS) {
        old_present = 1;
        if (old_type != REG_DWORD || old_size != sizeof(old_mode)) {
            r.win32_error = ERROR_DATATYPE_MISMATCH; RegCloseKey(key); goto close_adapters;
        }
    } else if (r.win32_error != ERROR_FILE_NOT_FOUND) { RegCloseKey(key); goto close_adapters; }
    DWORD enabled = 1;
    r.stage = "enable_test_mode";
    r.win32_error = RegSetValueExW(key, L"TdrTestMode", 0, REG_DWORD, (BYTE *)&enabled, sizeof(enabled));
    if (r.win32_error) { RegCloseKey(key); goto close_adapters; }
    r.stage = "force_tdr";
    r.result = "requesting";
    if (!save(out, &r)) { r.win32_error = ERROR_WRITE_FAULT; goto restore_test_mode; }
    UINT control = TDR_FORCE;
    escape_args e = {0};
    e.adapter = r.adapter.handle;
    e.type = ESCAPE_TDRDBGCTRL;
    e.data = &control;
    e.size = sizeof(control);
    r.escape_status = escape(&e);
    r.request_completed = 1;
    r.last_status = r.escape_status;
    r.result = r.escape_status >= 0 ? "request_accepted" : "api_failure";
    if (r.escape_status >= 0) {
        ULONGLONG until = GetTickCount64() + wait_ms;
        r.stage = "observe_recovery";
        do {
            r.telemetry_status = read_status(escape, r.adapter.handle, &r.after);
            r.after_valid = r.telemetry_status >= 0;
            r.recovered = r.before_valid && r.after_valid &&
                r.after.reset_count > r.before.reset_count && r.after.restart_count > r.before.restart_count &&
                r.after.last_reset_status == 0 && r.after.last_restart_status == 0 && r.after.recovery_state == 0;
            if (r.recovered) break;
            Sleep(50);
        } while (GetTickCount64() < until);
        exit_code = r.recovered ? 0 : 2;
    }
restore_test_mode:
    r.restore_status = old_present ? RegSetValueExW(key, L"TdrTestMode", 0, REG_DWORD, (BYTE *)&old_mode, sizeof(old_mode)) : RegDeleteValueW(key, L"TdrTestMode");
    RegCloseKey(key);
    if (r.restore_status != ERROR_SUCCESS) exit_code = 1;
close_adapters:
    for (ULONG i = 0; i < adapters.count; ++i) {
        close_adapter c = {adapters.adapters[i].handle};
        close(&c);
    }
    free(adapters.adapters);
    FreeLibrary(gdi);
done:
    if (!save(out, &r)) { r.win32_error = ERROR_WRITE_FAULT; exit_code = 1; }
    emit(stdout, &r);
    free(r.trace);
    return exit_code;
}

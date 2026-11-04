#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

using ULONGLONG = uint64_t;
using KSPIN_LOCK = unsigned;
using KIRQL = unsigned;
struct LIST_ENTRY { LIST_ENTRY *Flink, *Blink; };
using PLIST_ENTRY = LIST_ENTRY *;

constexpr unsigned PAGE_SIZE = 4096;
constexpr unsigned DISPATCH_LEVEL = 2;
constexpr unsigned NonPagedPoolNx = 0;
constexpr unsigned VIOGPUTAG = 0;
#define NT_ASSERT assert
#define CONTAINING_RECORD(address, type, field) \
    reinterpret_cast<type *>(reinterpret_cast<char *>(address) - offsetof(type, field))

inline void KeInitializeSpinLock(KSPIN_LOCK *) {}
inline unsigned KeGetCurrentIrql() { return 0; }
inline void KeAcquireSpinLock(KSPIN_LOCK *, KIRQL *old) { *old = 0; }
inline void KeReleaseSpinLock(KSPIN_LOCK *, KIRQL) {}
inline void InitializeListHead(LIST_ENTRY *head) { head->Flink = head->Blink = head; }
inline bool IsListEmpty(LIST_ENTRY *head) { return head->Flink == head; }
inline void RemoveEntryList(LIST_ENTRY *entry)
{
    entry->Blink->Flink = entry->Flink;
    entry->Flink->Blink = entry->Blink;
}
inline LIST_ENTRY *RemoveHeadList(LIST_ENTRY *head)
{
    LIST_ENTRY *entry = head->Flink;
    RemoveEntryList(entry);
    return entry;
}
inline void InsertTailList(LIST_ENTRY *head, LIST_ENTRY *entry)
{
    entry->Flink = head;
    entry->Blink = head->Blink;
    head->Blink->Flink = entry;
    head->Blink = entry;
}
inline void *ExAllocatePoolUninitialized(unsigned, size_t size, unsigned)
{
    return std::malloc(size);
}
inline void ExFreePoolWithTag(void *ptr, unsigned) { std::free(ptr); }

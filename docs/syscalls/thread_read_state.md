# zx_thread_read_state

## NAME

thread_read_state - Read one aspect of thread state.

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

zx_status_t zx_thread_read_state(
    zx_handle_t handle,
    uint32_t kind,
    void* buffer,
    size_t len,
    size_t actual[1]);
```

## DESCRIPTION

**thread_read_state**() reads one aspect of state of the thread. The thread
state may only be read when the thread is halted for an exception.

The thread state is highly processor specific. See the structures in
zircon/syscalls/debug.h for the contents of the structures on each platform.

## STATES

### ZX_THREAD_STATE_GENERAL_REGS

The buffer must point to a **zx_thread_state_general_regs_t** structure that
contains the general registers for the current architecture.

## RETURN VALUE

**thread_read_state**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not that of a thread.

**ZX_ERR_ACCESS_DENIED**  *handle* lacks *ZX_RIGHT_READ*.

**ZX_ERR_INVALID_ARGS**  *kind* is not valid,
or *buffer* or *actual* are invalid pointers,
or the provided *buffer_len* is too large.

**ZX_ERR_NO_MEMORY**  Temporary out of memory failure.

**ZX_ERR_BUFFER_TOO_SMALL**  The provided buffer is too small to hold *kind*.
The required size is returned in *actual[0]*.

**ZX_ERR_BAD_STATE**  The thread is not stopped at a point where state
is available. The thread state may only be read when the thread is stopped due
to an exception.

## SEE ALSO

[thread_write_state](thread_write_state.md).

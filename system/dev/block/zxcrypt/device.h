// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <crypto/cipher.h>
#include <ddk/device.h>
#include <ddk/iotxn.h>
#include <ddk/protocol/block.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/types.h>
#include <zx/port.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include "extra.h"
#include "worker.h"

namespace zxcrypt {

// TODO(aarongreen): Remove IotxnQueueable, DdkIotxnQueue once block protocol transition is complete
#define IOTXN_LEGACY_SUPPORT

// See ddk::Device in ddktl/device.h
class Device;
using DeviceType = ddk::Device<Device, ddk::Ioctlable, ddk::GetSizable,
#ifdef IOTXN_LEGACY_SUPPORT
                               ddk::IotxnQueueable,
#endif // IOTXN_LEGACY_SUPPORT
                               ddk::Unbindable>;

// |zxcrypt::Device| is an encrypted block device filter driver.  It binds to a block device and
// transparently encrypts writes to/decrypts reads from that device.  It shadows incoming requests
// with its own |zxcrypt::Op| request structure that uses a mapped VMO as working memory for
// cryptographic transformations.
class Device final : public DeviceType, public ddk::BlockProtocol<Device> {
public:
    // Maximum number of outstanding block operations.  The amount of memory used to buffer these
    // operations will be |kNumOps| blocks.
    static const size_t kMaxOps = 2048;

    explicit Device(zx_device_t* parent);
    ~Device();

    // Called via ioctl_device_bind.  This method sets up the synchronization primitives and starts
    // the |Init| thread.
    zx_status_t Bind();

    // The body of the |Init| thread.  This method attempts to cryptographically unseal the device
    // for normal operation, and adds it to the device tree if successful.
    zx_status_t Init() __TA_EXCLUDES(mtx_);

    // ddk::Device methods; see ddktl/device.h
    zx_status_t DdkIoctl(uint32_t op, const void* in, size_t in_len, void* out, size_t out_len,
                         size_t* actual) __TA_EXCLUDES(mtx_);
    zx_off_t DdkGetSize() __TA_EXCLUDES(mtx_);
#ifdef IOTXN_LEGACY_SUPPORT
    void DdkIotxnQueue(iotxn_t* txn);
#endif // IOTXN_LEGACY_SUPPORT
    void DdkUnbind() __TA_EXCLUDES(mtx_);
    void DdkRelease() __TA_EXCLUDES(mtx_);

    // ddk::BlockProtocol methods; see ddktl/protocol/block.h
    void BlockQuery(block_info_t* out_info, size_t* out_op_size) __TA_EXCLUDES(mtx_);
    void BlockQueue(block_op_t* block) __TA_EXCLUDES(mtx_);

    // Send |block| to the parent of the device stored in |txn->cookie|.
    void BlockForward(block_op_t* block) __TA_EXCLUDES(mtx_);

    // I/O callback invoked by the parent device.  Stored in |block->completion_cb| by |BlockQueue|.
    static void BlockComplete(block_op_t* block, zx_status_t rc) __TA_EXCLUDES(mtx_);

    // Completes a |block| returning from the parent device stored in |txn->cookie| and returns it
    // to the caller of |DdkIotxnQueue|.
    void BlockRelease(block_op_t* block, zx_status_t rc) __TA_EXCLUDES(mtx_);

    // Translates |block_op_t|s to |extra_op_t|s and vice versa.
    extra_op_t* BlockToExtra(block_op_t* block) const;
    block_op_t* ExtraToBlock(extra_op_t* extra) const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Device);

    // TODO(aarongreen): Investigate performance impact of changing this.
    // Number of encrypting/decrypting workers
    static const size_t kNumWorkers = 2;

#ifdef IOTXN_LEGACY_SUPPORT
    // Indicates the number of "adapter" |iotxn_t|s and |block_op_t|s available in the pools below
    // for translating between the legacy and current interface.
    static const size_t kNumAdapters = 256;
#endif // IOTXN_LEGACY_SUPPORT

    // Increments the amount of work this device has outstanding.  This should be called at the
    // start of any method that shouldn't be invoked after the device has been unbound.  It notably
    // is called in |Init| to prevent the work from dropping to zero before |DdkUnbind| is called.
    // Returns ZX_ERR_BAD_STATE if |DdkUnbind| has been called.
    zx_status_t AddTaskLocked() __TA_REQUIRES(mtx_);

    // Decrements the amount of work this task has outstanding.  This should be called whenever the
    // work started with a call to |addTask| completes.  When the amount of work reaches zero,
    // |DdkRemove| is called.  It notably is called in |DdkUnbind| to cause |DdkRemove| to be called
    // automatically once the device finishes its current workload.
    void FinishTaskLocked() __TA_REQUIRES(mtx_);

    // Scans the VMO for |len| blocks not in use, and returns the found VMO offset in |out|. If
    // |len| blocks cannot be found, it returns ZX_ERR_SHOULD_WAIT.
    zx_status_t AcquireBlocks(uint64_t len, uint64_t* out) __TA_EXCLUDES(mtx_);

    // Marks the blocks from |off| to |off + len| as available.  Signals waiting callers if
    // |AcquireBlocks| previously returned ZX_ERR_SHOULD_WAIT.
    void ReleaseBlocks(uint64_t off, uint64_t len) __TA_EXCLUDES(mtx_);

    // Take a |block|, associate with the memory reserved for it as indicated by the given |offset|,
    // and send it to a worker.
    void ProcessBlock(block_op_t* block, uint64_t offset) __TA_EXCLUDES(mtx_);

    // Defer this |block| request until later, due to insufficient memory for cryptographic
    // transformations.
    void EnqueueBlock(block_op_t* block) __TA_EXCLUDES(mtx_);

    // Returns a previously deferred block request, in the order they were submitted via
    // |EnqueueBlock|.
    block_op_t* DequeueBlock() __TA_EXCLUDES(mtx_);

    // Defers a previously deferred |block| request again, because there still aren't enough
    // resources available.  This differs from |EnqueueBlock| as |block| will be the next item
    // returned by |DequeueBlock|.
    void RequeueBlock(block_op_t* block) __TA_EXCLUDES(mtx_);

#ifdef IOTXN_LEGACY_SUPPORT
    // Scans the block adapters for a |block_op_t| not currently associated with an |iotxn_t| and
    // associates it with |txn|.  Returns ZX_ERR_BAD_STATE if the |DdkUnbind| has been called,
    // ZX_ERR_SHOULD_WAIT if no block adapters are available, and ZX_OK otherwise.
    zx_status_t AcquireBlockAdapter(iotxn_t* txn, block_op_t** out) __TA_EXCLUDES(mtx_);

    // Callback to a client using the legacy |DdkIotxnQueue| interface.  Translates the completed
    // |block_op_t| back into the original |iotxn_t| and completes it.
    static void BlockAdapterComplete(block_op_t* block, zx_status_t rc) __TA_EXCLUDES(mtx_);

    // Disassociates the |block| from its |iotxn_t|.
    void ReleaseBlockAdapter(block_op_t* block) __TA_EXCLUDES(mtx_);

    // Scans the block adapters for a |iotxn_t| not currently associated with an |block_op_t| and
    // associates it with |block|.  Returns ZX_ERR_BAD_STATE if the |DdkUnbind| has been called,
    // ZX_ERR_SHOULD_WAIT if no block adapters are available, and ZX_OK otherwise.
    zx_status_t AcquireIotxnAdapter(block_op_t* block, iotxn_t** out) __TA_EXCLUDES(mtx_);

    // Callback for a parent device using the legacy |iotxn_queue| interface.  Translates the
    // completed |iotxn_t| back into a corresponding |block_op_t| and completes it.
    static void IotxnAdapterComplete(iotxn_t* txn, void* cookie) __TA_EXCLUDES(mtx_);

    // Disassociates the |txn| from its |block_op_t|.
    void ReleaseIotxnAdapter(iotxn_t* txn) __TA_EXCLUDES(mtx_);

#endif // IOTXN_LEGACY_SUPPORT

    // Unsynchronized fields

    // This struct bundles several commonly accessed fields.  The bare pointer IS owned by the
    // object; it's "constness" prevents it from being an automatic pointer but allows it to be used
    // without holding the lock.  It is allocated and "constified" in |Init|, and |DdkRelease| must
    // "deconstify" and free it.
    struct DeviceInfo {
        // The parent block info, as modified by |zxcrypt::Volume::Init|
        block_info_t blk;
        // Indicates if the underlying device is an FVM partition.
        bool has_fvm;
        // The size of mapped VMO used to work on I/O transactions.
        size_t mapped_len;
        // The length of the reserved metadata, in bytes.
        uint64_t offset_dev;
        // The parent device's required block_op_t size.
        size_t op_size;
        // Callbacks to the parent's block protocol methods.
        block_protocol_t proto;
        // The ratio modified to unmodified parent block sizes.
        uint32_t scale;
        // A memory region used when encrypting/decrypting I/O transactions.
        zx::vmo vmo;
    };
    const DeviceInfo* info_;

    // Thread-related fields

    // The |Init| thread, used to configure and add the device.
    thrd_t init_;
    // Threads that performs encryption/decryption.
    Worker workers_[kNumWorkers];
    // Port used to send write/read operations to be encrypted/decrypted.
    zx::port port_;
    // Primary lock for accessing the fields below
    fbl::Mutex mtx_;

    // Synchronized fields

    // A cached copy of the parent device's FVM information, if applicable.
    fvm_info_t fvm_ __TA_GUARDED(mtx_);
    // Indicates whether this object is ready for I/O.
    bool active_ __TA_GUARDED(mtx_);
    // The number of outstanding tasks.  See |AddTask| and |FinishTask|.
    uint64_t tasks_;
    // Base address of the VMAR backing |rw_.vmo|.
    uintptr_t mapped_ __TA_GUARDED(mtx_);
    uint8_t* base_;

    // Indicates which ops (and corresponding blocks in the VMO) are in use.
    bitmap::RawBitmapGeneric<bitmap::FixedStorage<kMaxOps>> map_ __TA_GUARDED(mtx_);
    // Offset in the bitmap of the most recently allocated bit.
    size_t last_ __TA_GUARDED(mtx_);

    // Describes a queue of deferred block requests.
    extra_op_t* head_ __TA_GUARDED(mtx_);
    extra_op_t* tail_ __TA_GUARDED(mtx_);

#ifdef IOTXN_LEGACY_SUPPORT
    // A pool of |iotxn_t|s that can be borrowed to wrap |block_op_t|s when using the legacy
    // interface to the parent.
    fbl::unique_ptr<iotxn_t[]> iotxns_ __TA_GUARDED(mtx_);
    // Offset of the expected next available |iotxn_t|.
    size_t iotxns_head_ __TA_GUARDED(mtx_);
    // A pool of |block_op_t|s that can be borrowed to wrap |iotxn_t|s when supporting the legacy
    // interface to clients.  Note this is treated as a byte array to leave room for the overhead
    // described by |info_->op_size|.
    fbl::unique_ptr<uint8_t[]> blocks_ __TA_GUARDED(mtx_);
    // Offset of the expected next available |block_op_t|.
    size_t blocks_head_ __TA_GUARDED(mtx_);
#endif // IOTXN_LEGACY_SUPPORT
};

} // namespace zxcrypt

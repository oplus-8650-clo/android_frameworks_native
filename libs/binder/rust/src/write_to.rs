//! Trait implemented by all AIDL `@FixedSize` types.

use core::ptr;
use zerocopy::{Immutable, IntoBytes};

/// Trait for writing a struct to a byte buffer.
///
/// This will write all of the fields, skipping padding. The byte buffer written to should be
/// treated as a byte buffer rather than an instance of `Self` to avoid e.g. running destructors
/// twice.
pub trait WriteTo: Immutable + Sized {
    /// Write `self` to `target`, skipping padding.
    ///
    /// It is guaranteed that no uninitialized bytes are written to `target`.
    ///
    /// # Safety
    ///
    /// * The pointer must be valid for writing `size_of::<Self>()` bytes.
    /// * The pointer must be aligned to `align_of::<Self>()`.
    unsafe fn write_to(&self, target: *mut Self);

    /// Write `self` to `target` using one or more volatile writes, skipping padding.
    ///
    /// It is guaranteed that no uninitialized bytes are written to `target`.
    ///
    /// # Safety
    ///
    /// * The pointer must be valid for writing `size_of::<Self>()` bytes with volatile.
    /// * The pointer must be aligned to `align_of::<Self>()`.
    unsafe fn write_to_volatile(&self, target: *mut Self);
}

impl<T> WriteTo for T
where
    T: Immutable + IntoBytes,
{
    #[inline]
    unsafe fn write_to(&self, target: *mut Self) {
        // SAFETY:
        // * Caller ensures that `target` is valid for writing and sufficiently aligned.
        // * We know that `Self: Immutable` and `self` is a shared reference, so the bytes in
        //   `self` are not valid for writing during this call. Since `target` references only
        //   bytes that are valid for writing, this means that `self` and `target` do not overlap.
        unsafe { ptr::copy_nonoverlapping(self, target, 1) };
    }

    #[inline]
    unsafe fn write_to_volatile(&self, target: *mut Self) {
        // SAFETY: Caller ensures that `target` is valid for writing and sufficiently aligned.
        unsafe { target.write_volatile(ptr::read(self)) };
    }
}

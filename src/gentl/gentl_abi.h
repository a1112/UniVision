#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32) && (defined(_M_IX86) || defined(__i386__))
#  define UV_GENTL_CALL __stdcall
#else
#  define UV_GENTL_CALL
#endif

namespace univision::gentl::abi {

using Error = std::int32_t;
using InfoDataType = std::int32_t;
using InfoCommand = std::int32_t;
using Bool8 = std::uint8_t;
using Handle = void*;
using TLHandle = Handle;
using IFHandle = Handle;
using DevHandle = Handle;
using DSHandle = Handle;
using PortHandle = Handle;
using BufferHandle = Handle;
using EventHandle = Handle;

inline constexpr Error success = 0;
inline constexpr Error error = -1001;
inline constexpr Error not_initialized = -1002;
inline constexpr Error not_implemented = -1003;
inline constexpr Error resource_in_use = -1004;
inline constexpr Error access_denied = -1005;
inline constexpr Error invalid_handle = -1006;
inline constexpr Error invalid_id = -1007;
inline constexpr Error no_data = -1008;
inline constexpr Error invalid_parameter = -1009;
inline constexpr Error io_error = -1010;
inline constexpr Error timeout = -1011;
inline constexpr Error abort = -1012;
inline constexpr Error invalid_buffer = -1013;
inline constexpr Error not_available = -1014;
inline constexpr Error buffer_too_small = -1016;
inline constexpr Error resource_exhausted = -1020;
inline constexpr Error out_of_memory = -1021;
inline constexpr Error busy = -1022;

inline constexpr InfoDataType info_string = 1;
inline constexpr InfoDataType info_uint32 = 6;
inline constexpr InfoDataType info_uint64 = 8;
inline constexpr InfoDataType info_pointer = 10;
inline constexpr InfoDataType info_bool8 = 11;
inline constexpr InfoDataType info_size = 12;

inline constexpr InfoCommand tl_info_id = 0;
inline constexpr InfoCommand tl_info_vendor = 1;
inline constexpr InfoCommand tl_info_model = 2;
inline constexpr InfoCommand tl_info_version = 3;
inline constexpr InfoCommand tl_info_type = 4;
inline constexpr InfoCommand tl_info_name = 5;
inline constexpr InfoCommand tl_info_pathname = 6;
inline constexpr InfoCommand tl_info_display_name = 7;

inline constexpr InfoCommand device_info_id = 0;
inline constexpr InfoCommand device_info_vendor = 1;
inline constexpr InfoCommand device_info_model = 2;
inline constexpr InfoCommand device_info_type = 3;
inline constexpr InfoCommand device_info_display_name = 4;
inline constexpr InfoCommand device_info_user_name = 6;
inline constexpr InfoCommand device_info_serial = 7;

inline constexpr std::int32_t device_access_control = 3;

inline constexpr InfoCommand stream_info_payload_size = 7;
inline constexpr InfoCommand stream_info_num_underrun = 2;
inline constexpr InfoCommand stream_info_min_buffers = 12;

inline constexpr InfoCommand buffer_info_timestamp = 3;
inline constexpr InfoCommand buffer_info_incomplete = 7;
inline constexpr InfoCommand buffer_info_size_filled = 9;
inline constexpr InfoCommand buffer_info_width = 10;
inline constexpr InfoCommand buffer_info_height = 11;
inline constexpr InfoCommand buffer_info_xpadding = 14;
inline constexpr InfoCommand buffer_info_frame_id = 16;
inline constexpr InfoCommand buffer_info_image_offset = 18;
inline constexpr InfoCommand buffer_info_pixel_format = 20;
inline constexpr InfoCommand buffer_info_delivered_height = 22;
inline constexpr InfoCommand buffer_info_timestamp_ns = 28;

inline constexpr std::int32_t event_new_buffer = 1;
inline constexpr std::int32_t acquisition_start_default = 0;
inline constexpr std::int32_t acquisition_stop_default = 0;
inline constexpr std::int32_t acquisition_queue_all_discard = 4;
inline constexpr std::uint64_t infinite = ~std::uint64_t{0};

#pragma pack(push, 1)
struct NewBufferEventData {
  BufferHandle buffer_handle;
  void* user_pointer;
};
#pragma pack(pop)

using GCGetInfoFn = Error(UV_GENTL_CALL*)(InfoCommand, InfoDataType*, void*, std::size_t*);
using GCGetLastErrorFn = Error(UV_GENTL_CALL*)(Error*, char*, std::size_t*);
using GCInitLibFn = Error(UV_GENTL_CALL*)();
using GCCloseLibFn = Error(UV_GENTL_CALL*)();
using GCReadPortFn = Error(UV_GENTL_CALL*)(PortHandle, std::uint64_t, void*, std::size_t*);
using GCWritePortFn =
    Error(UV_GENTL_CALL*)(PortHandle, std::uint64_t, const void*, std::size_t*);
using GCGetPortURLFn = Error(UV_GENTL_CALL*)(PortHandle, char*, std::size_t*);
using GCRegisterEventFn = Error(UV_GENTL_CALL*)(Handle, std::int32_t, EventHandle*);
using GCUnregisterEventFn = Error(UV_GENTL_CALL*)(Handle, std::int32_t);
using EventGetDataFn = Error(UV_GENTL_CALL*)(EventHandle, void*, std::size_t*, std::uint64_t);
using EventKillFn = Error(UV_GENTL_CALL*)(EventHandle);
using TLOpenFn = Error(UV_GENTL_CALL*)(TLHandle*);
using TLCloseFn = Error(UV_GENTL_CALL*)(TLHandle);
using TLGetNumInterfacesFn = Error(UV_GENTL_CALL*)(TLHandle, std::uint32_t*);
using TLGetInterfaceIDFn =
    Error(UV_GENTL_CALL*)(TLHandle, std::uint32_t, char*, std::size_t*);
using TLOpenInterfaceFn = Error(UV_GENTL_CALL*)(TLHandle, const char*, IFHandle*);
using TLUpdateInterfaceListFn = Error(UV_GENTL_CALL*)(TLHandle, Bool8*, std::uint64_t);
using IFCloseFn = Error(UV_GENTL_CALL*)(IFHandle);
using IFGetNumDevicesFn = Error(UV_GENTL_CALL*)(IFHandle, std::uint32_t*);
using IFGetDeviceIDFn = Error(UV_GENTL_CALL*)(IFHandle, std::uint32_t, char*, std::size_t*);
using IFUpdateDeviceListFn = Error(UV_GENTL_CALL*)(IFHandle, Bool8*, std::uint64_t);
using IFGetDeviceInfoFn =
    Error(UV_GENTL_CALL*)(IFHandle, const char*, InfoCommand, InfoDataType*, void*, std::size_t*);
using IFOpenDeviceFn = Error(UV_GENTL_CALL*)(IFHandle, const char*, std::int32_t, DevHandle*);
using DevGetPortFn = Error(UV_GENTL_CALL*)(DevHandle, PortHandle*);
using DevGetNumDataStreamsFn = Error(UV_GENTL_CALL*)(DevHandle, std::uint32_t*);
using DevGetDataStreamIDFn =
    Error(UV_GENTL_CALL*)(DevHandle, std::uint32_t, char*, std::size_t*);
using DevOpenDataStreamFn = Error(UV_GENTL_CALL*)(DevHandle, const char*, DSHandle*);
using DevCloseFn = Error(UV_GENTL_CALL*)(DevHandle);
using DSAnnounceBufferFn =
    Error(UV_GENTL_CALL*)(DSHandle, void*, std::size_t, void*, BufferHandle*);
using DSFlushQueueFn = Error(UV_GENTL_CALL*)(DSHandle, std::int32_t);
using DSStartAcquisitionFn = Error(UV_GENTL_CALL*)(DSHandle, std::int32_t, std::uint64_t);
using DSStopAcquisitionFn = Error(UV_GENTL_CALL*)(DSHandle, std::int32_t);
using DSGetInfoFn =
    Error(UV_GENTL_CALL*)(DSHandle, InfoCommand, InfoDataType*, void*, std::size_t*);
using DSCloseFn = Error(UV_GENTL_CALL*)(DSHandle);
using DSRevokeBufferFn = Error(UV_GENTL_CALL*)(DSHandle, BufferHandle, void**, void**);
using DSQueueBufferFn = Error(UV_GENTL_CALL*)(DSHandle, BufferHandle);
using DSGetBufferInfoFn = Error(UV_GENTL_CALL*)(
    DSHandle, BufferHandle, InfoCommand, InfoDataType*, void*, std::size_t*);

}  // namespace univision::gentl::abi

// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.
// Contributors: https://github.com/UAVCAN/libcanard/contributors.
// READ THE DOCUMENTATION IN README.md.

#ifndef CANARD_H_INCLUDED
#define CANARD_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Semantic version of this library (not the UAVCAN specification).
/// API will be backward compatible within the same major version.
#define CANARD_VERSION_MAJOR 1

/// The version number of the UAVCAN specification implemented by this library.
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR 1

/// MTU values for supported protocols.
/// Per the recommendations given in the UAVCAN specification, other MTU values should not be used.
#define CANARD_MTU_CAN_CLASSIC 8U
#define CANARD_MTU_CAN_FD 64U

/// Parameter ranges are inclusive; the lower bound is zero for all. Refer to the specification for more info.
#define CANARD_SUBJECT_ID_MAX 32767U
#define CANARD_SERVICE_ID_MAX 511U
#define CANARD_NODE_ID_MAX 127U
#define CANARD_TRANSFER_ID_BIT_LENGTH 5U
#define CANARD_TRANSFER_ID_MAX ((1U << CANARD_TRANSFER_ID_BIT_LENGTH) - 1U)

/// This value represents an undefined node-ID: broadcast destination or anonymous source.
/// Library functions treat all values above @ref CANARD_NODE_ID_MAX as anonymous.
#define CANARD_NODE_ID_UNSET 255U

/// If not specified, the transfer-ID timeout will take this value for all new input sessions.
#define CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC 2000000UL

/// It is assumed that the native float type follows the IEEE 754 binary32 representation.
typedef float CanardIEEE754Binary32;

// Forward declarations.
typedef struct CanardInstance CanardInstance;

/// Transfer priority level mnemonics per the recommendations given in the UAVCAN specification.
typedef enum
{
    CanardPriorityExceptional = 0,
    CanardPriorityImmediate   = 1,
    CanardPriorityFast        = 2,
    CanardPriorityHigh        = 3,
    CanardPriorityNominal     = 4,  ///< Nominal priority level should be the default.
    CanardPriorityLow         = 5,
    CanardPrioritySlow        = 6,
    CanardPriorityOptional    = 7,
} CanardPriority;

/// Transfer kinds are defined by the UAVCAN specification.
typedef enum
{
    CanardTransferKindMessagePublication = 0,  ///< Multicast, from publisher to all subscribers.
    CanardTransferKindServiceResponse    = 1,  ///< Point-to-point, from server to client.
    CanardTransferKindServiceRequest     = 2,  ///< Point-to-point, from client to server.
} CanardTransferKind;

/// CAN data frame with an extended 29-bit ID. RTR/Error frames are not used and therefore not modeled here.
typedef struct
{
    /// For RX frames: reception timestamp.
    /// For TX frames: transmission deadline.
    /// The time system may be arbitrary as long as the clock is monotonic (steady) and 0 is not a valid timestamp.
    /// Zero timestamp indicates that the instance is invalid (empty).
    uint64_t timestamp_usec;

    /// 29-bit extended ID. The bits above 29-th are zero/ignored.
    uint32_t extended_can_id;

    /// The useful data in the frame. The length value is not to be confused with DLC!
    uint8_t payload_size;
    void*   payload;
} CanardCANFrame;

/// Conversion look-up tables between CAN DLC and data length.
extern const uint8_t CanardCANDLCToLength[16];
extern const uint8_t CanardCANLengthToDLC[65];

typedef struct
{
    /// For RX transfers: reception timestamp.
    /// For TX transfers: transmission deadline.
    /// The time system may be arbitrary as long as the clock is monotonic (steady) and 0 is not a valid timestamp.
    /// Zero timestamp indicates that the instance is invalid (empty).
    uint64_t timestamp_usec;

    CanardPriority priority;

    CanardTransferKind transfer_kind;

    /// Subject-ID for message publications; service-ID for service requests/responses.
    uint16_t port_id;

    uint8_t remote_node_id;

    uint8_t transfer_id;

    size_t payload_size;
    void*  payload;
} CanardTransfer;

/// The application supplies the library with this information when a new transfer should be received.
typedef struct
{
    /// The transfer-ID timeout for this session. If no specific requirements are defined, the default value
    /// @ref CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC should be used.
    /// Zero timeout indicates that this transfer should not be received (all its frames will be silently dropped).
    uint64_t transfer_id_timeout_usec;

    /// The maximum payload size of the transfer (i.e., the maximum size of the serialized DSDL object), in bytes.
    /// Payloads larger than this will be silently truncated per the Implicit Truncation Rule defined in Specification.
    /// Per Specification, the transfer CRC of multi-frame transfers is always validated regardless of the
    /// implicit truncation rule.
    /// Zero is also a valid value indicating that the transfer shall be accepted but the payload need not be stored.
    size_t payload_size_max;
} CanardRxMetadata;

/// The application shall implement this function and supply a pointer to it to the library during initialization.
/// The library calls this function to determine whether a transfer should be received.
/// @param ins            Library instance.
/// @param port_id        Subject-ID or service-ID of the transfer.
/// @param transfer_kind  Message or service transfer.
/// @param source_node_id Node-ID of the origin; @ref CANARD_NODE_ID_UNSET if anonymous.
/// @returns @ref CanardTransferReceptionParameters.
typedef CanardRxMetadata (*CanardRxFilter)(const CanardInstance* ins,
                                           uint16_t              port_id,
                                           CanardTransferKind    transfer_kind,
                                           uint8_t               source_node_id);

typedef void* (*CanardHeapAllocate)(CanardInstance* ins, size_t amount);

/// Free as in freedom.
typedef void (*CanardHeapFree)(CanardInstance* ins, void* pointer);

/// This is the core structure that keeps all of the states and allocated resources of the library instance.
/// Fields whose names begin with an underscore SHALL NOT be accessed by the application,
/// they are for internal use only.
struct CanardInstance
{
    /// User pointer that can link this instance with other objects.
    /// This field can be changed arbitrarily, the library does not access it after initialization.
    /// The default value is NULL.
    void* user_reference;

    /// The node-ID of the local node. The default value is @ref CANARD_NODE_ID_UNSET.
    /// Per the UAVCAN Specification, the node-ID should not be assigned more than once.
    /// Invalid values are treated as @ref CANARD_NODE_ID_UNSET.
    uint8_t node_id;

    /// The maximum transmission unit. The value can be changed arbitrarily at any time.
    /// This setting defines the maximum number of bytes per CAN data frame in all outgoing transfers.
    /// Regardless of this setting, CAN frames with any MTU can always be accepted.
    ///
    /// Only the standard values should be used as recommended by the specification; otherwise,
    /// networking interoperability issues may arise. See "CANARD_MTU_*".
    /// Valid values are any valid CAN frame data length. The default is the maximum valid value.
    /// Invalid values are treated as the nearest valid value.
    uint8_t mtu_bytes;

    /// Callbacks invoked by the library. See their type documentation for details.
    /// They SHALL be valid function pointers at all times.
    CanardHeapAllocate heap_allocate;
    CanardHeapFree     heap_free;
    CanardRxFilter     rx_filter;

    /// These fields are for internal use only. Do not access from the application.
    struct CanardInternalRxSession*   _rx_sessions;
    struct CanardInternalTxQueueItem* _tx_queue;
};

/// Initialize a new library instance.
/// The default values will be assigned as specified in the structure field documentation.
/// If any of the pointers are NULL, the behavior is undefined.
CanardInstance canardInit(const CanardHeapAllocate heap_allocate,
                          const CanardHeapFree     heap_free,
                          const CanardRxFilter     rx_filter);

void canardTxPush(CanardInstance* const ins, const CanardTransfer* const transfer);

CanardCANFrame canardTxPeek(const CanardInstance* const ins);

void canardTxPop(CanardInstance* const ins);

CanardTransfer canardRxPush(CanardInstance* const ins, const CanardCANFrame* const frame);

/// This function may be used to encode values for later transmission in a UAVCAN transfer.
/// It serializes a primitive value -- boolean, integer, character, or floating point -- and puts it at the
/// specified bit offset in the specified contiguous buffer.
/// Simple objects can also be serialized manually instead of using this function.
///
/// Caveat: This function works correctly only on platforms that use two's complement signed integer representation.
/// I am not aware of any modern microarchitecture that uses anything else than two's complement, so it should not
/// limit the portability.
///
/// The type of the value pointed to by 'value' is defined as follows:
///
///  | bit_length | value points to                          |
///  |------------|------------------------------------------|
///  | 1          | bool (may be incompatible with uint8_t!) |
///  | [2, 8]     | uint8_t, int8_t, or char                 |
///  | [9, 16]    | uint16_t, int16_t                        |
///  | [17, 32]   | uint32_t, int32_t, or 32-bit float       |
///  | [33, 64]   | uint64_t, int64_t, or 64-bit float       |
///
/// @param destination   Destination buffer where the result will be stored.
/// @param offset_bit    Offset, in bits, from the beginning of the destination buffer.
/// @param length_bit    Length of the value, in bits; see the table.
/// @param value         Pointer to the value; see the table.
void canardDSDLPrimitiveSerialize(void* const       destination,
                                  const size_t      offset_bit,
                                  const uint8_t     length_bit,
                                  const void* const value);

/// This function may be used to extract values from received UAVCAN transfers. It decodes a scalar value --
/// boolean, integer, character, or floating point -- from the specified bit position in the source buffer.
///
/// Caveat: This function works correctly only on platforms that use two's complement signed integer representation.
/// I am not aware of any modern microarchitecture that uses anything else than two's complement, so it should not
/// limit the portability.
///
/// The type of the value pointed to by 'out_value' is defined as follows:
///
///  | bit_length | is_signed   | out_value points to                      |
///  |------------|-------------|------------------------------------------|
///  | 1          | false       | bool (may be incompatible with uint8_t!) |
///  | 1          | true        | N/A                                      |
///  | [2, 8]     | false       | uint8_t, or char                         |
///  | [2, 8]     | true        | int8_t, or char                          |
///  | [9, 16]    | false       | uint16_t                                 |
///  | [9, 16]    | true        | int16_t                                  |
///  | [17, 32]   | false       | uint32_t                                 |
///  | [17, 32]   | true        | int32_t, or 32-bit float IEEE 754        |
///  | [33, 64]   | false       | uint64_t                                 |
///  | [33, 64]   | true        | int64_t, or 64-bit float IEEE 754        |
///
/// @param source       The source buffer where the data will be read from.
/// @param offset_bit   Offset, in bits, from the beginning of the source buffer.
/// @param length_bit   Length of the value, in bits; see the table.
/// @param is_signed    True if the value can be negative (i.e., sign bit extension is needed); see the table.
/// @param out_value    Pointer to the output storage; see the table.
void canardDSDLPrimitiveDeserialize(const void* const source,
                                    const size_t      offset_bit,
                                    const uint8_t     length_bit,
                                    const bool        is_signed,
                                    void* const       out_value);

/// IEEE 754 binary16 marshaling helpers.
/// These functions convert between the native float and the standard IEEE 754 binary16 float (a.k.a. half precision).
/// It is assumed that the native float is IEEE 754 binary32, otherwise, the results may be unpredictable.
/// Majority of modern computers and microcontrollers use IEEE 754, so this limitation should not limit the portability.
uint16_t              canardDSDLFloat16Serialize(const CanardIEEE754Binary32 value);
CanardIEEE754Binary32 canardDSDLFloat16Deserialize(const uint16_t value);

#ifdef __cplusplus
}
#endif
#endif

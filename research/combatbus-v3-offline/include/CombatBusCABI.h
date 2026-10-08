#ifndef IIF_COMBATBUS_C_ABI_H
#define IIF_COMBATBUS_C_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define IIF_CB_CALL __cdecl
#if defined(IIF_CB_HOST_EXPORTS)
#define IIF_CB_API __declspec(dllexport)
#else
#define IIF_CB_API __declspec(dllimport)
#endif
#else
#define IIF_CB_CALL
#define IIF_CB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define IIF_CB_VERSION_3 UINT32_C(3)

/* Stable C ABI statuses. Every status is returned/stored as uint32_t. */
#define IIF_CB_STATUS_OK UINT32_C(0)
#define IIF_CB_STATUS_UNSUPPORTED_VERSION UINT32_C(1)
#define IIF_CB_STATUS_INVALID_ARGUMENT UINT32_C(2)
#define IIF_CB_STATUS_INVALID_STRUCT_SIZE UINT32_C(3)
#define IIF_CB_STATUS_SHUTTING_DOWN UINT32_C(4)
#define IIF_CB_STATUS_INVALID_PROVIDER UINT32_C(5)
#define IIF_CB_STATUS_DUPLICATE UINT32_C(6)
#define IIF_CB_STATUS_ALLOCATION_FAILURE UINT32_C(7)
#define IIF_CB_STATUS_HANDLE_EXHAUSTED UINT32_C(8)
#define IIF_CB_STATUS_NOT_FOUND UINT32_C(9)
#define IIF_CB_STATUS_WOULD_DEADLOCK UINT32_C(10)
#define IIF_CB_STATUS_WAIT_FAILURE UINT32_C(11)
#define IIF_CB_STATUS_INTERNAL_ERROR UINT32_C(12)
/* Another WaitQuiescent call owns the single-consumer claim. */
#define IIF_CB_STATUS_WAIT_IN_PROGRESS UINT32_C(13)

#define IIF_CB_CALLBACK_NO_CHANGE UINT32_C(0)
#define IIF_CB_CALLBACK_APPLY UINT32_C(1)
#define IIF_CB_CALLBACK_FAILURE UINT32_C(2)

#define IIF_CB_DISPATCH_APPLIED UINT32_C(0)
#define IIF_CB_DISPATCH_NO_PROVIDERS UINT32_C(1)
#define IIF_CB_DISPATCH_NO_DAMAGE UINT32_C(2)
#define IIF_CB_DISPATCH_INVALID_CONTEXT UINT32_C(3)
#define IIF_CB_DISPATCH_INVALID_INPUT UINT32_C(4)
#define IIF_CB_DISPATCH_PROVIDER_FAILURE UINT32_C(5)
#define IIF_CB_DISPATCH_INVALID_PROVIDER_RESULT UINT32_C(6)
#define IIF_CB_DISPATCH_RECURSIVE UINT32_C(7)
#define IIF_CB_DISPATCH_NO_CHANGE UINT32_C(8)
#define IIF_CB_DISPATCH_CLOSED UINT32_C(9)

#define IIF_CB_STAGE_OUTGOING_CALCULATION UINT32_C(1)
#define IIF_CB_STAGE_INCOMING_HEALTH UINT32_C(2)

#define IIF_CB_COMPONENT_HEALTH (UINT32_C(1) << 0)
#define IIF_CB_COMPONENT_PHYSICAL (UINT32_C(1) << 1)
#define IIF_CB_COMPONENT_TOTAL (UINT32_C(1) << 2)
#define IIF_CB_COMPONENT_TARGETED_LIMB (UINT32_C(1) << 3)
#define IIF_CB_COMPONENT_RESISTANCE (UINT32_C(1) << 4)

#define IIF_CB_EVALUATION_CALCULATION (UINT32_C(1) << 0)
#define IIF_CB_EVALUATION_PREDICTION (UINT32_C(1) << 1)

#define IIF_CB_EVALUATION_UNKNOWN UINT32_C(0)
#define IIF_CB_EVALUATION_CALCULATION_KIND UINT32_C(1)
#define IIF_CB_EVALUATION_PREDICTION_KIND UINT32_C(2)
#define IIF_CB_ACTOR_UNKNOWN UINT32_C(0)
#define IIF_CB_ACTOR_PLAYER UINT32_C(1)
#define IIF_CB_ACTOR_NPC UINT32_C(2)
#define IIF_CB_ACTOR_OTHER UINT32_C(3)
#define IIF_CB_PROFILE_UNKNOWN UINT32_C(0)
#define IIF_CB_PROFILE_WEAPON_DIRECT UINT32_C(1)
#define IIF_CB_PROFILE_WEAPON_MELEE UINT32_C(2)
#define IIF_CB_PROFILE_WEAPON_PROJECTILE UINT32_C(3)
#define IIF_CB_INCOMING_PHASE_UNKNOWN UINT32_C(0)
#define IIF_CB_INCOMING_HEALTH_AFTER_RESISTANCE_BEFORE_DIFFICULTY UINT32_C(1)
#define IIF_CB_CONFIDENCE_UNKNOWN UINT32_C(0)
#define IIF_CB_CONFIDENCE_VERIFIED_ADAPTER_CALLSITE UINT32_C(1)
#define IIF_CB_POWER_ARMOR_UNKNOWN UINT32_C(0)
#define IIF_CB_POWER_ARMOR_NOT_EQUIPPED UINT32_C(1)
#define IIF_CB_POWER_ARMOR_EQUIPPED UINT32_C(2)

typedef struct IIF_CB_DamageSnapshotV3 {
	uint32_t struct_size;
	uint32_t valid_mask;
	float health_damage;
	float physical_damage;
	float total_damage;
	float targeted_limb_damage;
	float resistance_intermediate;
} IIF_CB_DamageSnapshotV3;

typedef struct IIF_CB_OutgoingContextV3 {
	uint32_t struct_size;
	uint32_t version;
	uint32_t evaluation_kind;
	uint32_t attacker_kind;
	uint32_t profile;
	uint32_t modifiable_mask;
	void* attacker; /* borrowed for one callback; never retain */
	void* target;   /* optional, borrowed for one callback; never retain */
	void* weapon;   /* borrowed for one callback; never retain */
	IIF_CB_DamageSnapshotV3 damage;
} IIF_CB_OutgoingContextV3;

typedef struct IIF_CB_OutgoingResultV3 {
	uint32_t struct_size;
	uint32_t version;
	uint32_t status;
	uint32_t component_mask;
	float multiplier;
} IIF_CB_OutgoingResultV3;

typedef struct IIF_CB_IncomingContextV3 {
	uint32_t struct_size;
	uint32_t version;
	uint32_t phase;
	uint32_t confidence;
	uint32_t target_kind;
	uint32_t power_armor;
	void* attacker; /* optional borrowed handle; never retain */
	void* target;   /* borrowed for one callback; never retain */
	void* weapon;   /* optional borrowed handle; never retain */
	float health_damage;
} IIF_CB_IncomingContextV3;

typedef struct IIF_CB_IncomingResultV3 {
	uint32_t struct_size;
	uint32_t version;
	uint32_t status;
	uint32_t reserved;
	float multiplier;
} IIF_CB_IncomingResultV3;

typedef struct IIF_CB_ProviderHandleV3 {
	uint64_t value;
} IIF_CB_ProviderHandleV3;

typedef uint32_t (IIF_CB_CALL *IIF_CB_OutgoingCallbackV3)(void* provider_context,
	const IIF_CB_OutgoingContextV3* context, IIF_CB_OutgoingResultV3* result);
typedef uint32_t (IIF_CB_CALL *IIF_CB_IncomingCallbackV3)(void* provider_context,
	const IIF_CB_IncomingContextV3* context, IIF_CB_IncomingResultV3* result);

typedef struct IIF_CB_OutgoingProviderV3 {
	uint32_t struct_size;
	uint32_t version;
	const char* provider_id; /* borrowed only until register returns; Host copies it */
	int32_t priority;
	uint32_t evaluation_mask;
	void* provider_context; /* Provider-owned; valid until unregister + quiescence */
	IIF_CB_OutgoingCallbackV3 callback; /* Provider DLL code; keep loaded until quiescent */
} IIF_CB_OutgoingProviderV3;

typedef struct IIF_CB_IncomingProviderV3 {
	uint32_t struct_size;
	uint32_t version;
	const char* provider_id; /* borrowed only until register returns; Host copies it */
	int32_t priority;
	uint32_t reserved;
	void* provider_context; /* Provider-owned; valid until unregister + quiescence */
	IIF_CB_IncomingCallbackV3 callback; /* Provider DLL code; keep loaded until quiescent */
} IIF_CB_IncomingProviderV3;

typedef struct IIF_CB_RegistrationV3 {
	uint32_t struct_size;
	uint32_t version;
	uint32_t status;
	uint32_t added;
	IIF_CB_ProviderHandleV3 handle;
} IIF_CB_RegistrationV3;

typedef struct IIF_CB_OutgoingDispatchV3 {
	uint32_t struct_size;
	uint32_t version;
	uint32_t status;
	uint32_t changed_mask;
	IIF_CB_DamageSnapshotV3 damage;
} IIF_CB_OutgoingDispatchV3;

typedef struct IIF_CB_IncomingDispatchV3 {
	uint32_t struct_size;
	uint32_t version;
	uint32_t status;
	float health_damage;
} IIF_CB_IncomingDispatchV3;

struct IIF_CB_InterfaceV3;
typedef uint32_t (IIF_CB_CALL *IIF_CB_RegisterOutgoingFn)(void* registry,
	const IIF_CB_OutgoingProviderV3* provider, IIF_CB_RegistrationV3* result);
typedef uint32_t (IIF_CB_CALL *IIF_CB_RegisterIncomingFn)(void* registry,
	const IIF_CB_IncomingProviderV3* provider, IIF_CB_RegistrationV3* result);
typedef uint32_t (IIF_CB_CALL *IIF_CB_UnregisterFn)(void* registry,
	IIF_CB_ProviderHandleV3 handle, uint32_t stage);
typedef uint32_t (IIF_CB_CALL *IIF_CB_WaitQuiescentFn)(void* registry,
	IIF_CB_ProviderHandleV3 handle, uint32_t stage);
typedef uint32_t (IIF_CB_CALL *IIF_CB_DispatchOutgoingFn)(void* registry,
	const IIF_CB_OutgoingContextV3* context, IIF_CB_OutgoingDispatchV3* result);
typedef uint32_t (IIF_CB_CALL *IIF_CB_DispatchIncomingFn)(void* registry,
	const IIF_CB_IncomingContextV3* context, IIF_CB_IncomingDispatchV3* result);

typedef struct IIF_CB_InterfaceV3 {
	uint32_t struct_size;
	uint32_t version;
	void* registry; /* Host-owned opaque object, valid only while Host DLL is loaded */
	IIF_CB_RegisterOutgoingFn register_outgoing;
	IIF_CB_RegisterIncomingFn register_incoming;
	IIF_CB_UnregisterFn unregister_provider;
	IIF_CB_WaitQuiescentFn wait_provider_quiescent;
	IIF_CB_DispatchOutgoingFn dispatch_outgoing;
	IIF_CB_DispatchIncomingFn dispatch_incoming;
} IIF_CB_InterfaceV3;

typedef uint32_t (IIF_CB_CALL *IIF_CB_ShutdownFn)(void);

/* Query copies a function table into caller storage; caller_size and the input
 * struct_size must both exactly match V3. The copy owns no registry state. */
IIF_CB_API uint32_t IIF_CB_CALL IIF_CombatBus_QueryInterface(uint32_t requested_version,
	uint32_t caller_size, IIF_CB_InterfaceV3* out_interface);

/* Owner must stop/join new callers before this call. It closes the Host gate,
 * drains entered API calls, then closes the Dispatcher. Do not call from a callback. */
IIF_CB_API uint32_t IIF_CB_CALL IIF_CombatBus_Shutdown(void);

#ifdef __cplusplus
}

static_assert(sizeof(IIF_CB_DamageSnapshotV3) == 28);
static_assert(offsetof(IIF_CB_DamageSnapshotV3, resistance_intermediate) == 24);
static_assert(sizeof(IIF_CB_OutgoingContextV3) == 80);
static_assert(offsetof(IIF_CB_OutgoingContextV3, attacker) == 24);
static_assert(offsetof(IIF_CB_OutgoingContextV3, damage) == 48);
static_assert(sizeof(IIF_CB_OutgoingResultV3) == 20);
static_assert(sizeof(IIF_CB_IncomingContextV3) == 56);
static_assert(offsetof(IIF_CB_IncomingContextV3, health_damage) == 48);
static_assert(sizeof(IIF_CB_OutgoingProviderV3) == 40);
static_assert(sizeof(IIF_CB_IncomingProviderV3) == 40);
static_assert(sizeof(IIF_CB_ProviderHandleV3) == 8);
static_assert(sizeof(IIF_CB_RegistrationV3) == 24);
static_assert(offsetof(IIF_CB_RegistrationV3, handle) == 16);
static_assert(sizeof(IIF_CB_OutgoingDispatchV3) == 44);
static_assert(sizeof(IIF_CB_IncomingDispatchV3) == 16);
static_assert(sizeof(IIF_CB_InterfaceV3) == 64);
static_assert(offsetof(IIF_CB_InterfaceV3, register_outgoing) == 16);
static_assert(offsetof(IIF_CB_InterfaceV3, dispatch_incoming) == 56);
#else
_Static_assert(sizeof(IIF_CB_DamageSnapshotV3) == 28, "damage layout");
_Static_assert(_Alignof(IIF_CB_DamageSnapshotV3) == 4, "damage alignment");
_Static_assert(offsetof(IIF_CB_DamageSnapshotV3, resistance_intermediate) == 24, "damage offset");
_Static_assert(sizeof(IIF_CB_OutgoingContextV3) == 80, "outgoing context layout");
_Static_assert(_Alignof(IIF_CB_OutgoingContextV3) == 8, "outgoing context alignment");
_Static_assert(offsetof(IIF_CB_OutgoingContextV3, attacker) == 24, "outgoing attacker offset");
_Static_assert(offsetof(IIF_CB_OutgoingContextV3, damage) == 48, "outgoing damage offset");
_Static_assert(sizeof(IIF_CB_OutgoingResultV3) == 20, "outgoing result layout");
_Static_assert(offsetof(IIF_CB_OutgoingResultV3, multiplier) == 16, "outgoing multiplier offset");
_Static_assert(sizeof(IIF_CB_IncomingContextV3) == 56, "incoming context layout");
_Static_assert(_Alignof(IIF_CB_IncomingContextV3) == 8, "incoming context alignment");
_Static_assert(offsetof(IIF_CB_IncomingContextV3, attacker) == 24, "incoming attacker offset");
_Static_assert(sizeof(IIF_CB_IncomingResultV3) == 20, "incoming result layout");
_Static_assert(offsetof(IIF_CB_IncomingResultV3, multiplier) == 16, "incoming multiplier offset");
_Static_assert(sizeof(IIF_CB_OutgoingProviderV3) == 40, "outgoing Provider layout");
_Static_assert(offsetof(IIF_CB_OutgoingProviderV3, callback) == 32, "outgoing callback offset");
_Static_assert(sizeof(IIF_CB_IncomingProviderV3) == 40, "incoming Provider layout");
_Static_assert(offsetof(IIF_CB_IncomingProviderV3, callback) == 32, "incoming callback offset");
_Static_assert(sizeof(IIF_CB_InterfaceV3) == 64, "interface layout");
_Static_assert(_Alignof(IIF_CB_InterfaceV3) == 8, "interface alignment");
_Static_assert(sizeof(IIF_CB_RegistrationV3) == 24, "registration layout");
_Static_assert(offsetof(IIF_CB_RegistrationV3, handle) == 16, "registration handle offset");
_Static_assert(sizeof(IIF_CB_OutgoingDispatchV3) == 44, "outgoing dispatch layout");
_Static_assert(sizeof(IIF_CB_IncomingDispatchV3) == 16, "incoming dispatch layout");
_Static_assert(offsetof(IIF_CB_InterfaceV3, registry) == 8, "interface registry offset");
_Static_assert(offsetof(IIF_CB_InterfaceV3, register_outgoing) == 16, "interface registration offset");
_Static_assert(offsetof(IIF_CB_InterfaceV3, wait_provider_quiescent) == 40, "interface wait offset");
_Static_assert(offsetof(IIF_CB_InterfaceV3, dispatch_incoming) == 56, "interface incoming offset");
#endif

/* Keep the full Win64 public-layout contract checked in both C and C++. */
#ifdef __cplusplus
#define IIF_CB_LAYOUT_ASSERT(expression) static_assert((expression), #expression)
#define IIF_CB_LAYOUT_ALIGNOF(type) alignof(type)
#else
#define IIF_CB_LAYOUT_ASSERT(expression) _Static_assert((expression), #expression)
#define IIF_CB_LAYOUT_ALIGNOF(type) _Alignof(type)
#endif

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_DamageSnapshotV3) == 28);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_DamageSnapshotV3) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_DamageSnapshotV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_DamageSnapshotV3, valid_mask) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_DamageSnapshotV3, health_damage) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_DamageSnapshotV3, physical_damage) == 12);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_DamageSnapshotV3, total_damage) == 16);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_DamageSnapshotV3, targeted_limb_damage) == 20);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_DamageSnapshotV3, resistance_intermediate) == 24);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_OutgoingContextV3) == 80);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_OutgoingContextV3) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, evaluation_kind) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, attacker_kind) == 12);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, profile) == 16);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, modifiable_mask) == 20);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, attacker) == 24);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, target) == 32);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, weapon) == 40);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingContextV3, damage) == 48);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_OutgoingResultV3) == 20);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_OutgoingResultV3) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingResultV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingResultV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingResultV3, status) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingResultV3, component_mask) == 12);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingResultV3, multiplier) == 16);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_IncomingContextV3) == 56);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_IncomingContextV3) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, phase) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, confidence) == 12);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, target_kind) == 16);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, power_armor) == 20);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, attacker) == 24);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, target) == 32);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, weapon) == 40);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingContextV3, health_damage) == 48);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_IncomingResultV3) == 20);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_IncomingResultV3) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingResultV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingResultV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingResultV3, status) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingResultV3, reserved) == 12);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingResultV3, multiplier) == 16);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_OutgoingProviderV3) == 40);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_OutgoingProviderV3) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingProviderV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingProviderV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingProviderV3, provider_id) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingProviderV3, priority) == 16);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingProviderV3, evaluation_mask) == 20);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingProviderV3, provider_context) == 24);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingProviderV3, callback) == 32);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_IncomingProviderV3) == 40);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_IncomingProviderV3) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingProviderV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingProviderV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingProviderV3, provider_id) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingProviderV3, priority) == 16);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingProviderV3, reserved) == 20);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingProviderV3, provider_context) == 24);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingProviderV3, callback) == 32);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_ProviderHandleV3) == 8);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_ProviderHandleV3) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_ProviderHandleV3, value) == 0);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_RegistrationV3) == 24);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_RegistrationV3) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_RegistrationV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_RegistrationV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_RegistrationV3, status) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_RegistrationV3, added) == 12);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_RegistrationV3, handle) == 16);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_OutgoingDispatchV3) == 44);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_OutgoingDispatchV3) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingDispatchV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingDispatchV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingDispatchV3, status) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingDispatchV3, changed_mask) == 12);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_OutgoingDispatchV3, damage) == 16);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_IncomingDispatchV3) == 16);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_IncomingDispatchV3) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingDispatchV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingDispatchV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingDispatchV3, status) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_IncomingDispatchV3, health_damage) == 12);

IIF_CB_LAYOUT_ASSERT(sizeof(IIF_CB_InterfaceV3) == 64);
IIF_CB_LAYOUT_ASSERT(IIF_CB_LAYOUT_ALIGNOF(IIF_CB_InterfaceV3) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, struct_size) == 0);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, version) == 4);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, registry) == 8);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, register_outgoing) == 16);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, register_incoming) == 24);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, unregister_provider) == 32);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, wait_provider_quiescent) == 40);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, dispatch_outgoing) == 48);
IIF_CB_LAYOUT_ASSERT(offsetof(IIF_CB_InterfaceV3, dispatch_incoming) == 56);

#undef IIF_CB_LAYOUT_ALIGNOF
#undef IIF_CB_LAYOUT_ASSERT

#endif /* IIF_COMBATBUS_C_ABI_H */

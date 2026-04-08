/**
 * @brief Simple logger.
 *
 * Writes the output into file inside the current directory.
 */

// Windows stuff.

#ifdef _WIN32
#  define WINVER 0x0500
#  define _WIN32_WINNT 0x0500
#  include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <libserial/SerialPort.h>
using namespace LibSerial;
// SDK

#include "scssdk_telemetry.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"

#define UNUSED(x)

/**
 * @brief Logging support.
 */
FILE *log_file = NULL;

/**
 * @brief Serial port
 */
SerialPort serial_port;

/**
 * @brief Tracking of paused state of the game.
 */
bool output_paused = true;

/**
 * @brief Should we print the data header next time
 * we are printing the data?
 */
bool print_header = true;

/**
 * @brief Last timestamp we received.
 */
scs_timestamp_t last_timestamp = static_cast<scs_timestamp_t>(-1);

/**
 * @brief Combined telemetry data.
 */
struct telemetry_state_t {
    int gear;
    int hshifter_slot;
    float engineRPM;
    /*
    using the gear and hshifter slot we can determine if it's grinding
    if the gear reported by the engine is 0 it's in neutral
    if the hshifter is 0, it's in neutral
    if (telemetry.hshifter_slot != 0 && telemetry.gear == 0)
    {
        Yummy gear shavings
    }

    */
} telemetry;

/**
 * @brief Function writting message to the game internal log.
 */
scs_log_t game_log = NULL;


SCSAPI_VOID telemetry_frame_end(const scs_event_t UNUSED(event), const void *const UNUSED(event_info),
                                const scs_context_t UNUSED(context)) {
    if (output_paused) {
        return;
    }

    char grinding = telemetry.hshifter_slot != 0 && telemetry.gear == 0 && telemetry.engineRPM != 0;
    serial_port.WriteByte((char) (grinding << 5 | telemetry.hshifter_slot) );

}

SCSAPI_VOID telemetry_pause(const scs_event_t event, const void *const UNUSED(event_info),
                            const scs_context_t UNUSED(context)) {
    output_paused = (event == SCS_TELEMETRY_EVENT_paused);
}


// Handling of individual channels.
SCSAPI_VOID telemetry_store_float(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value, const scs_context_t context)
{
    // The SCS_TELEMETRY_CHANNEL_FLAG_no_value flag was not provided during registration
    // so this callback is only called when a valid value is available.

    assert(value);
    assert(value->type == SCS_VALUE_TYPE_float);
    assert(context);
    *static_cast<float *>(context) = value->value_float.value;
}

SCSAPI_VOID telemetry_store_s32(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value,
                                const scs_context_t context) {
    // The SCS_TELEMETRY_CHANNEL_FLAG_no_value flag was not provided during registration
    // so this callback is only called when a valid value is available.

    assert(value);
    assert(value->type == SCS_VALUE_TYPE_s32);
    assert(context);
    *static_cast<int *>(context) = value->value_s32.value;
}

SCSAPI_VOID telemetry_store_u32(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value,
                                const scs_context_t context) {
    // The SCS_TELEMETRY_CHANNEL_FLAG_no_value flag was not provided during registration
    // so this callback is only called when a valid value is available.

    assert(value);
    assert(value->type == SCS_VALUE_TYPE_u32);
    assert(context);
    *static_cast<int *>(context) = value->value_u32.value;
}

void init_serial() {
    serial_port.Open("/dev/ttyACM0");
    if (!serial_port.IsOpen())
        serial_port.Open("/dev/ttyACM1");
    serial_port.SetBaudRate(BaudRate::BAUD_115200);
    serial_port.WriteByte('Q'); //Send Quit in case config left on
    serial_port.WriteByte('G'); //Actuate the block to prove the connection

}


void finish_serial() {
    serial_port.Close();
}


/**
 * @brief Telemetry API initialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t *const params) {
    // We currently support only one version.

    if (version != SCS_TELEMETRY_VERSION_1_01) {
        return SCS_RESULT_unsupported;
    }

    const scs_telemetry_init_params_v101_t *const version_params = static_cast<const scs_telemetry_init_params_v101_t *>
            (params);


    // Register for events. Note that failure to register those basic events
    // likely indicates invalid usage of the api or some critical problem. As the
    // example requires all of them, we can not continue if the registration fails.

    const bool events_registered =
            // (version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_start, telemetry_frame_start, NULL) == SCS_RESULT_ok) &&
            (version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_end, telemetry_frame_end, NULL) ==
             SCS_RESULT_ok) &&
            (version_params->register_for_event(SCS_TELEMETRY_EVENT_paused, telemetry_pause, NULL) == SCS_RESULT_ok) &&
            (version_params->register_for_event(SCS_TELEMETRY_EVENT_started, telemetry_pause, NULL) == SCS_RESULT_ok);
    if (!events_registered) {
        // Registrations created by unsuccessfull initialization are
        // cleared automatically so we can simply exit.

        version_params->common.log(SCS_LOG_TYPE_error, "Unable to register event callbacks");
        return SCS_RESULT_generic_error;
    }

    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none,
        telemetry_store_float, &telemetry.engineRPM);
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear, SCS_U32_NIL, SCS_VALUE_TYPE_s32, SCS_TELEMETRY_CHANNEL_FLAG_none,
        telemetry_store_s32, &telemetry.gear);
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_hshifter_slot, SCS_U32_NIL, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_none,
        telemetry_store_u32, &telemetry.hshifter_slot);

    // Remember the function we will use for logging.

    game_log = version_params->common.log;
    game_log(SCS_LOG_TYPE_message, "Initializing telemetry");

    // Set the structure with defaults.

    memset(&telemetry, 0, sizeof(telemetry));
    print_header = true;
    last_timestamp = static_cast<scs_timestamp_t>(-1);


    init_serial();
    // Initially the game is paused.

    output_paused = true;
    return SCS_RESULT_ok;
}

/**
 * @brief Telemetry API deinitialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_VOID scs_telemetry_shutdown(void) {
    // Any cleanup needed. The registrations will be removed automatically
    // so there is no need to do that manually.

    game_log = NULL;
}

// Cleanup

#ifdef _WIN32
BOOL APIENTRY DllMain(
	HMODULE module,
	DWORD  reason_for_call,
	LPVOID reseved
)
{
	if (reason_for_call == DLL_PROCESS_DETACH) {
		finish_log();
	}
	return TRUE;
}
#endif

#ifdef __linux__
void __attribute__ ((destructor)) unload(void) {
    finish_serial();
}
#endif

#include "../metroflip_i.h"
#include <datetime.h>
#include <dolphin/dolphin.h>
#include <locale/locale.h>
#include "navigo.h"

#include <nfc/protocols/iso14443_4b/iso14443_4b_poller.h>

#define TAG "Metroflip:Scene:Navigo"

void metroflip_navigo_widget_callback(GuiButtonType result, InputType type, void* context) {
    Metroflip* app = context;
    UNUSED(result);

    if(type == InputTypeShort) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, MetroflipSceneStart);
    }
}

static NfcCommand metroflip_scene_navigo_poller_callback(NfcGenericEvent event, void* context) {
    furi_assert(event.protocol == NfcProtocolIso14443_4b);
    NfcCommand next_command = NfcCommandContinue;
    MetroflipPollerEventType stage = MetroflipPollerEventTypeStart;

    Metroflip* app = context;
    FuriString* parsed_data = furi_string_alloc();
    Widget* widget = app->widget;
    furi_string_reset(app->text_box_store);

    const Iso14443_4bPollerEvent* iso14443_4b_event = event.event_data;

    Iso14443_4bPoller* iso14443_4b_poller = event.instance;

    BitBuffer* tx_buffer = bit_buffer_alloc(Metroflip_POLLER_MAX_BUFFER_SIZE);
    BitBuffer* rx_buffer = bit_buffer_alloc(Metroflip_POLLER_MAX_BUFFER_SIZE);

    if(iso14443_4b_event->type == Iso14443_4bPollerEventTypeReady) {
        if(stage == MetroflipPollerEventTypeStart) {
            nfc_device_set_data(
                app->nfc_device, NfcProtocolIso14443_4b, nfc_poller_get_data(app->poller));

            Iso14443_4bError error;
            size_t response_length = 0;

            do {
                // Select app for contracts
                select_app[6] = 32;
                bit_buffer_reset(tx_buffer);
                bit_buffer_append_bytes(tx_buffer, select_app, sizeof(select_app));
                error = iso14443_4b_poller_send_block(iso14443_4b_poller, tx_buffer, rx_buffer);
                if(error != Iso14443_4bErrorNone) {
                    FURI_LOG_I(TAG, "Select File: iso14443_4b_poller_send_block error %d", error);
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFail);
                    break;
                }

                // Check the response after selecting app
                response_length = bit_buffer_get_size_bytes(rx_buffer);
                if(bit_buffer_get_byte(rx_buffer, response_length - 2) != apdu_success[0] ||
                   bit_buffer_get_byte(rx_buffer, response_length - 1) != apdu_success[1]) {
                    FURI_LOG_I(
                        TAG,
                        "Select profile app failed: %02x%02x",
                        bit_buffer_get_byte(rx_buffer, response_length - 2),
                        bit_buffer_get_byte(rx_buffer, response_length - 1));
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFileNotFound);
                    break;
                }

                // read file 1
                read_file[2] = 1;
                bit_buffer_reset(tx_buffer);
                bit_buffer_append_bytes(tx_buffer, read_file, sizeof(read_file));
                error = iso14443_4b_poller_send_block(iso14443_4b_poller, tx_buffer, rx_buffer);
                if(error != Iso14443_4bErrorNone) {
                    FURI_LOG_I(TAG, "Read File: iso14443_4b_poller_send_block error %d", error);
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFail);
                    break;
                }

                // Check the response after reading the file
                response_length = bit_buffer_get_size_bytes(rx_buffer);
                if(bit_buffer_get_byte(rx_buffer, response_length - 2) != apdu_success[0] ||
                   bit_buffer_get_byte(rx_buffer, response_length - 1) != apdu_success[1]) {
                    FURI_LOG_I(
                        TAG,
                        "Read file failed: %02x%02x",
                        bit_buffer_get_byte(rx_buffer, response_length - 2),
                        bit_buffer_get_byte(rx_buffer, response_length - 1));
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFileNotFound);
                    break;
                }
                char bit_representation[response_length * 8 + 1];
                bit_representation[0] = '\0';
                for(size_t i = 0; i < response_length; i++) {
                    char bits[9];
                    uint8_t byte = bit_buffer_get_byte(rx_buffer, i);
                    byte_to_binary(byte, bits);
                    strlcat(bit_representation, bits, sizeof(bit_representation));
                }
                bit_representation[response_length * 8] = '\0';
                int start = 55, end = 70;
                float decimal_value = bit_slice_to_dec(bit_representation, start, end);
                float balance = decimal_value / 100;
                furi_string_printf(parsed_data, "\e#Navigo:\n\n");
                furi_string_cat_printf(parsed_data, "\e#Contract 1:\n");
                furi_string_cat_printf(parsed_data, "Balance: %.2f EUR\n", (double)balance);
                start = 80, end = 93;
                decimal_value = bit_slice_to_dec(bit_representation, start, end);
                uint64_t start_date_timestamp = (decimal_value * 24 * 3600) + (float)epoch + 3600;
                DateTime start_dt = {0};
                datetime_timestamp_to_datetime(start_date_timestamp, &start_dt);
                furi_string_cat_printf(parsed_data, "\nStart Date:\n");
                locale_format_datetime_cat(parsed_data, &start_dt, false);
                furi_string_cat_printf(parsed_data, "\n");

                // Select app for environment
                select_app[6] = 1;
                bit_buffer_reset(tx_buffer);
                bit_buffer_append_bytes(tx_buffer, select_app, sizeof(select_app));
                error = iso14443_4b_poller_send_block(iso14443_4b_poller, tx_buffer, rx_buffer);
                if(error != Iso14443_4bErrorNone) {
                    FURI_LOG_I(TAG, "Select File: iso14443_4b_poller_send_block error %d", error);
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFail);
                    break;
                }

                // Check the response after selecting app
                response_length = bit_buffer_get_size_bytes(rx_buffer);
                if(bit_buffer_get_byte(rx_buffer, response_length - 2) != apdu_success[0] ||
                   bit_buffer_get_byte(rx_buffer, response_length - 1) != apdu_success[1]) {
                    FURI_LOG_I(
                        TAG,
                        "Select profile app failed: %02x%02x",
                        bit_buffer_get_byte(rx_buffer, response_length - 2),
                        bit_buffer_get_byte(rx_buffer, response_length - 1));
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFileNotFound);
                    break;
                }

                // read file 1
                read_file[2] = 1;
                bit_buffer_reset(tx_buffer);
                bit_buffer_append_bytes(tx_buffer, read_file, sizeof(read_file));
                error = iso14443_4b_poller_send_block(iso14443_4b_poller, tx_buffer, rx_buffer);
                if(error != Iso14443_4bErrorNone) {
                    FURI_LOG_I(TAG, "Read File: iso14443_4b_poller_send_block error %d", error);
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFail);
                    break;
                }

                // Check the response after reading the file
                response_length = bit_buffer_get_size_bytes(rx_buffer);
                if(bit_buffer_get_byte(rx_buffer, response_length - 2) != apdu_success[0] ||
                   bit_buffer_get_byte(rx_buffer, response_length - 1) != apdu_success[1]) {
                    FURI_LOG_I(
                        TAG,
                        "Read file failed: %02x%02x",
                        bit_buffer_get_byte(rx_buffer, response_length - 2),
                        bit_buffer_get_byte(rx_buffer, response_length - 1));
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFileNotFound);
                    break;
                }
                char environment_bit_representation[response_length * 8 + 1];
                environment_bit_representation[0] = '\0';
                for(size_t i = 0; i < response_length; i++) {
                    char bits[9];
                    uint8_t byte = bit_buffer_get_byte(rx_buffer, i);
                    byte_to_binary(byte, bits);
                    strlcat(
                        environment_bit_representation,
                        bits,
                        sizeof(environment_bit_representation));
                }
                start = 45;
                end = 58;
                decimal_value = bit_slice_to_dec(environment_bit_representation, start, end);
                uint64_t end_validity_timestamp =
                    (decimal_value * 24 * 3600) + (float)epoch + 3600;
                DateTime end_dt = {0};
                datetime_timestamp_to_datetime(end_validity_timestamp, &end_dt);
                furi_string_cat_printf(parsed_data, "\nEnd Validity:\n");
                locale_format_datetime_cat(parsed_data, &end_dt, false);
                furi_string_cat_printf(parsed_data, "\n\n");

                // Select app for events
                select_app[6] = 16;
                bit_buffer_reset(tx_buffer);
                bit_buffer_append_bytes(tx_buffer, select_app, sizeof(select_app));
                error = iso14443_4b_poller_send_block(iso14443_4b_poller, tx_buffer, rx_buffer);
                if(error != Iso14443_4bErrorNone) {
                    FURI_LOG_I(TAG, "Select File: iso14443_4b_poller_send_block error %d", error);
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFail);
                    break;
                }

                // Check the response after selecting app
                response_length = bit_buffer_get_size_bytes(rx_buffer);
                if(bit_buffer_get_byte(rx_buffer, response_length - 2) != apdu_success[0] ||
                   bit_buffer_get_byte(rx_buffer, response_length - 1) != apdu_success[1]) {
                    FURI_LOG_I(
                        TAG,
                        "Select events app failed: %02x%02x",
                        bit_buffer_get_byte(rx_buffer, response_length - 2),
                        bit_buffer_get_byte(rx_buffer, response_length - 1));
                    stage = MetroflipPollerEventTypeFail;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, MetroflipCustomEventPollerFileNotFound);
                    break;
                }

                furi_string_cat_printf(parsed_data, "\e#Events:\n");
                // Now send the read command
                for(size_t i = 1; i < 4; i++) {
                    read_file[2] = i;
                    bit_buffer_reset(tx_buffer);
                    bit_buffer_append_bytes(tx_buffer, read_file, sizeof(read_file));
                    error =
                        iso14443_4b_poller_send_block(iso14443_4b_poller, tx_buffer, rx_buffer);
                    if(error != Iso14443_4bErrorNone) {
                        FURI_LOG_I(
                            TAG, "Read File: iso14443_4b_poller_send_block error %d", error);
                        stage = MetroflipPollerEventTypeFail;
                        view_dispatcher_send_custom_event(
                            app->view_dispatcher, MetroflipCustomEventPollerFail);
                        break;
                    }

                    // Check the response after reading the file
                    response_length = bit_buffer_get_size_bytes(rx_buffer);
                    if(bit_buffer_get_byte(rx_buffer, response_length - 2) != apdu_success[0] ||
                       bit_buffer_get_byte(rx_buffer, response_length - 1) != apdu_success[1]) {
                        FURI_LOG_I(
                            TAG,
                            "Read file failed: %02x%02x",
                            bit_buffer_get_byte(rx_buffer, response_length - 2),
                            bit_buffer_get_byte(rx_buffer, response_length - 1));
                        stage = MetroflipPollerEventTypeFail;
                        view_dispatcher_send_custom_event(
                            app->view_dispatcher, MetroflipCustomEventPollerFileNotFound);
                        break;
                    }
                    char event_bit_representation[response_length * 8 + 1];
                    event_bit_representation[0] = '\0';
                    for(size_t i = 0; i < response_length; i++) {
                        char bits[9];
                        uint8_t byte = bit_buffer_get_byte(rx_buffer, i);
                        byte_to_binary(byte, bits);
                        strlcat(event_bit_representation, bits, sizeof(event_bit_representation));
                    }
                    furi_string_cat_printf(parsed_data, "\nEvent 0%d:\n", i);
                    int start = 53, end = 60;
                    int decimal_value = bit_slice_to_dec(event_bit_representation, start, end);
                    int transport_type = decimal_value >> 4;
                    int transition = decimal_value & 15;
                    furi_string_cat_printf(
                        parsed_data,
                        "%s - %s\n",
                        TRANSPORT_LIST[transport_type],
                        TRANSITION_LIST[transition]);
                    start = 69, end = 84;
                    decimal_value = bit_slice_to_dec(event_bit_representation, start, end);
                    int line_id = (decimal_value >> 9) - 1;
                    int station_id = ((decimal_value >> 4) & 31) - 1;
                    furi_string_cat_printf(
                        parsed_data,
                        "Line: %s\nStation: %s\n",
                        METRO_LIST[line_id].name,
                        METRO_LIST[line_id].stations[station_id]);
                    start = 61, end = 68;
                    decimal_value = bit_slice_to_dec(event_bit_representation, start, end);
                    furi_string_cat_printf(
                        parsed_data, "Provider: %s\n", SERVICE_PROVIDERS[decimal_value]);
                    start = 0, end = 13;
                    decimal_value = bit_slice_to_dec(event_bit_representation, start, end);
                    uint64_t date_timestamp = (decimal_value * 24 * 3600) + epoch + 3600;
                    DateTime dt = {0};
                    datetime_timestamp_to_datetime(date_timestamp, &dt);
                    furi_string_cat_printf(parsed_data, "Time: ");
                    locale_format_datetime_cat(parsed_data, &dt, false);
                    start = 14, end = 24;
                    decimal_value = bit_slice_to_dec(event_bit_representation, start, end);
                    furi_string_cat_printf(
                        parsed_data,
                        " %02d:%02d:%02d\n\n",
                        ((decimal_value * 60) / 3600),
                        (((decimal_value * 60) % 3600) / 60),
                        (((decimal_value * 60) % 3600) % 60));
                }

                widget_add_text_scroll_element(
                    widget, 0, 0, 128, 64, furi_string_get_cstr(parsed_data));

                widget_add_button_element(
                    widget, GuiButtonTypeRight, "Exit", metroflip_navigo_widget_callback, app);

                furi_string_free(parsed_data);
                view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewWidget);
                metroflip_app_blink_stop(app);
                stage = MetroflipPollerEventTypeSuccess;
                next_command = NfcCommandStop;
            } while(false);

            if(stage != MetroflipPollerEventTypeSuccess) {
                next_command = NfcCommandStop;
            }
        }
    }
    bit_buffer_free(tx_buffer);
    bit_buffer_free(rx_buffer);

    return next_command;
}

void metroflip_scene_navigo_on_enter(void* context) {
    Metroflip* app = context;
    dolphin_deed(DolphinDeedNfcRead);

    // Setup view
    Popup* popup = app->popup;
    popup_set_header(popup, "Apply\n card to\nthe back", 68, 30, AlignLeft, AlignTop);
    popup_set_icon(popup, 0, 3, &I_RFIDDolphinReceive_97x61);

    // Start worker
    view_dispatcher_switch_to_view(app->view_dispatcher, MetroflipViewPopup);
    nfc_scanner_alloc(app->nfc);
    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolIso14443_4b);
    nfc_poller_start(app->poller, metroflip_scene_navigo_poller_callback, app);

    metroflip_app_blink_start(app);
}

bool metroflip_scene_navigo_on_event(void* context, SceneManagerEvent event) {
    Metroflip* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MetroflipPollerEventTypeCardDetect) {
            Popup* popup = app->popup;
            popup_set_header(popup, "Scanning..", 68, 30, AlignLeft, AlignTop);
            consumed = true;
        } else if(event.event == MetroflipCustomEventPollerFileNotFound) {
            Popup* popup = app->popup;
            popup_set_header(popup, "Read Error,\n wrong card", 68, 30, AlignLeft, AlignTop);
            consumed = true;
        } else if(event.event == MetroflipCustomEventPollerFail) {
            Popup* popup = app->popup;
            popup_set_header(popup, "Error, try\n again", 68, 30, AlignLeft, AlignTop);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, MetroflipSceneStart);
        consumed = true;
    }

    return consumed;
}

void metroflip_scene_navigo_on_exit(void* context) {
    Metroflip* app = context;

    if(app->poller) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
    }
    metroflip_app_blink_stop(app);
    widget_reset(app->widget);

    // Clear view
    popup_reset(app->popup);
}
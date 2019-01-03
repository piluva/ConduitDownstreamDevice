// Connect lora server to iot edge as downstream device.


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>


#include "parson.h"
#include "mqtt_client_sample.h"

#include "azure_umqtt_c/mqtt_client.h"

#include "iothub.h"
#include "iothub_device_client.h"
#include "iothub_client_options.h"
#include "iothub_message.h"
#include "iothubtransportmqtt.h"

#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/platform.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "azure_c_shared_utility/socketio.h"

// Connection String
static const char* connectionString = "HostName=mqttHubCert.azure-devices.net;DeviceId=mqttConduit2;SharedAccessKey=dS0GsY94Wlgsf1rDlRqTED0b54smjkypNFXrP7yrctg=;GatewayHostName=192.168.1.44";
//static const char* connectionString = "HostName=mqttHubCert.azure-devices.net;DeviceId=mqttConduit1;SharedAccessKey=bj6Prj/C+hE0MjZIy4TGVp0QHcfEDUuJdGIUuqyhMWc=;GatewayHostName=192.168.1.26";

// Path to the Edge "owner" root CA certificate
static const char* edge_ca_cert_path = "/home/azure-iot-test-only.root.ca.cert.pem";

char *send_json_string = NULL; //final json string to iothub

//mqtt lora client variables
static const char* TOPIC_NAME_A = "lora/+/up";

static uint16_t PACKET_ID_VALUE = 11;

static bool g_continue = true;
static bool g_close_complete = true;

static bool new_message = false;

static size_t g_message_count_send_confirmations = 0;

MQTT_MESSAGE_HANDLE lastMsgHandle;

#define PORT_NUM_UNENCRYPTED        1883

#define DEFAULT_MSG_TO_SEND         1

static void send_confirm_callback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
    (void)userContextCallback;
    // When a message is sent this callback will get envoked
    g_message_count_send_confirmations++;
    (void)printf("Confirmation callback received for message %zu with result %s\r\n", g_message_count_send_confirmations, ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
}

/**
    MQTT LORA FUNCTIONS
*/

static const char* QosToString(QOS_VALUE qosValue)
{
    switch (qosValue)
    {
        case DELIVER_AT_LEAST_ONCE: return "Deliver_At_Least_Once";
        case DELIVER_EXACTLY_ONCE: return "Deliver_Exactly_Once";
        case DELIVER_AT_MOST_ONCE: return "Deliver_At_Most_Once";
        case DELIVER_FAILURE: return "Deliver_Failure";
    }
    return "";
}

static void OnRecvCallback(MQTT_MESSAGE_HANDLE msgHandle, void* context)
{

    (void)context;
    const APP_PAYLOAD* appMsg = mqttmessage_getApplicationMsg(msgHandle);

    (void)printf("Incoming Msg: Packet Id: %d\r\nQOS: %s\r\nTopic Name: %s\r\n",
        mqttmessage_getPacketId(msgHandle),
        QosToString(mqttmessage_getQosType(msgHandle) ),
        mqttmessage_getTopicName(msgHandle)
        );

  // parse incoming mqtt lora message as parson
	JSON_Value *lora_value=NULL;
	lora_value = json_parse_string(appMsg->message);
	JSON_Object *lora_json;
	if(json_value_get_type(lora_value) == JSONObject)
	{
    lora_json = json_value_get_object(lora_value);

    //make json to send
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    //put desired propieties on json to send
    const char *time_ = json_object_get_string(lora_json, "time");
		if(time_!=NULL)(void)json_object_set_string(root_object, "time", time_);

    const char *deveui = json_object_get_string(lora_json, "deveui");
		if(deveui!=NULL)(void)json_object_set_string(root_object, "deveui", deveui);

    send_json_string = json_serialize_to_string_pretty(root_value);
    json_value_free(root_value);

    //set flag to send
   new_message = true;

	}
	if(lora_value)json_value_free(lora_value);

  (void)printf("\r\n");

}

static void OnCloseComplete(void* context)
{
    (void)context;

    (void)printf("On Close Connection called\r\n");
    g_close_complete = false;
}

static void OnOperationComplete(MQTT_CLIENT_HANDLE handle, MQTT_CLIENT_EVENT_RESULT actionResult, const void* msgInfo, void* callbackCtx)
{
    (void)msgInfo;
    (void)callbackCtx;
    switch (actionResult)
    {
        case MQTT_CLIENT_ON_CONNACK:
        {
            (void)printf("ConnAck function called\r\n");

            SUBSCRIBE_PAYLOAD subscribe[1];
            subscribe[0].subscribeTopic = TOPIC_NAME_A;
            subscribe[0].qosReturn = DELIVER_AT_MOST_ONCE;

            if (mqtt_client_subscribe(handle, PACKET_ID_VALUE++, subscribe, sizeof(subscribe) / sizeof(subscribe[0])) != 0)
            {
                (void)printf("%d: mqtt_client_subscribe failed\r\n", __LINE__);
                g_continue = false;
            }
            break;
        }

        case MQTT_CLIENT_ON_SUBSCRIBE_ACK:
        {
            break;
        }

        case MQTT_CLIENT_ON_PUBLISH_ACK:
        {
            break;
        }
        case MQTT_CLIENT_ON_PUBLISH_RECV:
        {
            break;
        }
        case MQTT_CLIENT_ON_PUBLISH_REL:
        {
            break;
        }
        case MQTT_CLIENT_ON_PUBLISH_COMP:
        {
             break;
        }
        case MQTT_CLIENT_ON_DISCONNECT:
            g_continue = false;
            break;
        case MQTT_CLIENT_ON_UNSUBSCRIBE_ACK:
        {
            break;
        }
        case MQTT_CLIENT_ON_PING_RESPONSE:
            break;
        default:
        {
            (void)printf("unexpected value received for enumeration (%d)\n", (int)actionResult);
        }
    }
}

static void OnErrorComplete(MQTT_CLIENT_HANDLE handle, MQTT_CLIENT_EVENT_ERROR error, void* callbackCtx)
{
    (void)callbackCtx;
    (void)handle;
    switch (error)
    {
    case MQTT_CLIENT_CONNECTION_ERROR:
    case MQTT_CLIENT_PARSE_ERROR:
    case MQTT_CLIENT_MEMORY_ERROR:
    case MQTT_CLIENT_COMMUNICATION_ERROR:
    case MQTT_CLIENT_NO_PING_RESPONSE:
    case MQTT_CLIENT_UNKNOWN_ERROR:
		(void)printf("%d: error! \r\n", error);
        g_continue = false;
        break;
    }
}

/**
    Connection status
*/
static void connection_status_callback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* user_context)
{
    (void)reason;
    (void)user_context;
    // This sample DOES NOT take into consideration network outages.
    if (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED)
    {
        (void)printf("The device client is connected to IoT Edge\r\n");
    }
    else
    {
        (void)printf("The device client has been disconnected\r\n");
    }
}


/**
    Read the certificate file and provide a null terminated string
    containing the certificate.
*/
static char *obtain_edge_ca_certificate(void)
{
    char *result = NULL;
    FILE *ca_file;

    ca_file = fopen(edge_ca_cert_path, "r");
    if (ca_file == NULL)
    {
        printf("Error could not open file for reading %s\r\n", edge_ca_cert_path);
    }
    else
    {
        size_t file_size;

        (void)fseek(ca_file, 0, SEEK_END);
        file_size = ftell(ca_file);
        (void)fseek(ca_file, 0, SEEK_SET);
        // increment size to hold the null term
        file_size += 1;

        if (file_size == 0) // check wrap around
        {
            printf("File size invalid for %s\r\n", edge_ca_cert_path);
        }
        else
        {
            result = (char*)calloc(file_size, 1);
            if (result == NULL)
            {
                printf("Could not allocate memory to hold the certificate\r\n");
            }
            else
            {
                // copy the file into the buffer
                size_t read_size = fread(result, 1, file_size - 1, ca_file);
                if (read_size != file_size - 1)
                {
                    printf("Error reading file %s\r\n", edge_ca_cert_path);
                    free(result);
                    result = NULL;
                }
            }
        }
        (void)fclose(ca_file);
    }

    return result;
}


///// ------------------------------------   MAIN
int main(void)
{

	IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol;
	protocol = MQTT_Protocol;
	IOTHUB_DEVICE_CLIENT_HANDLE device_handle;

	const char* telemetry_msg = "test_message";
  char *cert_string = NULL;


  printf("MULTITECH CONDUIT DOWSTREAM DEVICE V0.1 2018\r\n\r\n");

	// Used to initialize IoTHub SDK subsystem
  (void)IoTHub_Init();

 	(void)printf("Creating IoTHub handle\r\n");
    // Create the iothub handle here
    device_handle = IoTHubDeviceClient_CreateFromConnectionString(connectionString, protocol);

	if (device_handle == NULL)
    {
        (void)printf("Failure creating device client handle.  Hint: Check you connection string.\r\n");
    }
    else
    {
        // Setting message callback to get C2D messages
        //(void)IoTHubDeviceClient_SetMessageCallback(device_handle, receive_msg_callback, NULL);
        // Setting connection status callback to get indication of connection to iothub
        (void)IoTHubDeviceClient_SetConnectionStatusCallback(device_handle, connection_status_callback, NULL);

        // Set any option that are necessary.
        // For available options please see the iothub_sdk_options.md documentation

        // Turn tracing on/off
        bool traceOn = true;
        (void)IoTHubDeviceClient_SetOption(device_handle, OPTION_LOG_TRACE, &traceOn);

        // Provide the Azure IoT device client with the Edge root
        // X509 CA certificate that was used to setup the Edge runtime
        cert_string = obtain_edge_ca_certificate();
        (void)IoTHubDeviceClient_SetOption(device_handle, OPTION_TRUSTED_CERT, cert_string);

        //Setting the auto URL Encoder (recommended for MQTT). Please use this option unless
        //you are URL Encoding inputs yourself.
        //ONLY valid for use with MQTT
        bool urlEncodeOn = true;
        (void)IoTHubDeviceClient_SetOption(device_handle, OPTION_AUTO_URL_ENCODE_DECODE, &urlEncodeOn);

		    //Mqtt client to recive lora messages from conduit lora server. localhost lora/+/up
        MQTT_CLIENT_HANDLE mqttHandle = mqtt_client_init(OnRecvCallback, OnOperationComplete, NULL, OnErrorComplete, NULL);
        if (mqttHandle == NULL)
        {
            (void)printf("mqtt_client_init failed\r\n");
        }
        else
        {
            mqtt_client_set_trace(mqttHandle, false, false);

            MQTT_CLIENT_OPTIONS options = { 0 };
            options.clientId = "csdk"; //id
            options.willMessage = NULL;
            options.username = NULL;
            options.password = NULL;
            options.keepAliveInterval = 10;
            options.useCleanSession = true;
            options.qualityOfServiceValue = DELIVER_AT_MOST_ONCE;

            SOCKETIO_CONFIG config = {"127.0.0.1", PORT_NUM_UNENCRYPTED, NULL}; //host name to local host

            XIO_HANDLE xio = xio_create(socketio_get_interface_description(), &config);
            if (xio == NULL)
            {
                (void)printf("xio_create failed\r\n");
            }
            else
            {
                if (mqtt_client_connect(mqttHandle, xio, &options) != 0)
                {
                    (void)printf("mqtt_client_connect failed\r\n");
                }
                else
                {
                    do
                    {
                        mqtt_client_dowork(mqttHandle);
						if(new_message){
							new_message = false;

              IOTHUB_MESSAGE_HANDLE message_handle;

							// Construct the iothub message from a string
							message_handle = IoTHubMessage_CreateFromString(send_json_string);

							// Set Message property
							(void)IoTHubMessage_SetMessageId(message_handle, "MSG_ID");
							(void)IoTHubMessage_SetCorrelationId(message_handle, "CORE_ID");
							//(void)IoTHubMessage_SetContentTypeSystemProperty(message_handle, "application%2fjson");
							(void)IoTHubMessage_SetContentEncodingSystemProperty(message_handle, "utf-8");

							// Add custom properties to message
							//(void)IoTHubMessage_SetProperty(message_handle, "property_key", "property_value");

							(void)printf("Sending message: %s\r\n", send_json_string);
							IoTHubDeviceClient_SendEventAsync(device_handle, message_handle, send_confirm_callback, NULL);

              json_free_serialized_string(send_json_string);

							// The message is copied to the sdk so the we can destroy it
							IoTHubMessage_Destroy(message_handle);
						}
                    } while (g_continue);
                  }
                xio_close(xio, OnCloseComplete, NULL);

                // Wait for the close connection gets called
                do
                {
                    mqtt_client_dowork(mqttHandle);
                } while (g_close_complete);
                xio_destroy(xio);
            }
            mqtt_client_deinit(mqttHandle);

		}

 		printf("\r\nPress any key to continue\r\n");
        (void)getchar();

        // Clean up the iothub sdk handle
        IoTHubDeviceClient_Destroy(device_handle);

        if (cert_string != NULL)
        {
            free(cert_string);
            cert_string = NULL;
        }
	}

 	// Free all the sdk subsystem
    IoTHub_Deinit();

 	return 0;
}

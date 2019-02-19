// Connect lora server to iot edge as downstream device.


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <getopt.h>

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
#include "azure_c_shared_utility/base64.h"


// Path to the Edge "owner" root CA certificate
static const char* edge_ca_cert_path = NULL; //"/home/azure-iot-test-only.root.ca.cert.pem";

// Path to the Edge "owner" root CA certificate
static const char* conn_string_file = NULL; //"/home/CDS.conf";

//mqtt lora client variables
static const char* TOPIC_NAME = "lora/+/up";

//IOT HUB
IOTHUB_DEVICE_CLIENT_HANDLE device_handle;

const unsigned char* TwinPayload;

//uMQTT
MQTT_MESSAGE_HANDLE lastMsgHandle;

static uint16_t PACKET_ID_VALUE = 11;

static bool g_continue = true;
static bool g_close_complete = true;

static bool new_message = false;

static size_t g_message_count_send_confirmations = 0;

#define PORT_NUM_UNENCRYPTED        1883

static int verbose_flag;

// string splitting
int split (const char *txt, char delim, char ***tokens)
{
    int *tklen, *t, count = 1;
    char **arr, *p = (char *) txt;

    while (*p != '\0') if (*p++ == delim) count += 1;
    t = tklen = calloc (count, sizeof (int));
    for (p = (char *) txt; *p != '\0'; p++) *p == delim ? *t++ : (*t)++;
    *tokens = arr = malloc (count * sizeof (char *));
    t = tklen;
    p = *arr++ = calloc (*(t++) + 1, sizeof (char *));
    while (*txt != '\0')
    {
        if (*txt == delim)
        {
            p = *arr++ = calloc (*(t++) + 1, sizeof (char *));
            txt++;
        }
        else *p++ = *txt++;
    }
    free (tklen);
    return count;
}

// --------------------------------------------------------------------

/**
    IOT_HUB FUNCTIONS
*/

static void deviceTwinCallback(DEVICE_TWIN_UPDATE_STATE update_state, const unsigned char* payLoad, size_t size, void* userContextCallback)
{
    (void)update_state;
    (void)size;

    if (update_state == DEVICE_TWIN_UPDATE_COMPLETE)
    {
        TwinPayload = payLoad;
        (void) printf("TWIN UPDATED: %s\r\n", TwinPayload);
    }
}

static void send_confirm_callback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
    (void)userContextCallback;
    // When a message is sent this callback will get envoked
    g_message_count_send_confirmations++;
    if(verbose_flag){
      (void)printf("Confirmation callback received for message %zu with result %s\r\n", g_message_count_send_confirmations, ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
    }
}

void sendToHub(char* message)
{
  if(verbose_flag){
    (void)printf("Sending message: %s\r\n", message);
  }
  IOTHUB_MESSAGE_HANDLE message_handle;

  // Construct the iothub message from a string
  message_handle = IoTHubMessage_CreateFromString(message);

  // Set Message property
  (void)IoTHubMessage_SetMessageId(message_handle, "MSG_ID");
  (void)IoTHubMessage_SetCorrelationId(message_handle, "CORE_ID");

  //(void)IoTHubMessage_SetContentTypeSystemProperty(message_handle, "application%2fjson");
  (void)IoTHubMessage_SetContentEncodingSystemProperty(message_handle, "utf-8");

  // Add custom properties to message
  //(void)IoTHubMessage_SetProperty(message_handle, "property_key", "property_value");
  IoTHubDeviceClient_SendEventAsync(device_handle, message_handle, send_confirm_callback, NULL);

  json_free_serialized_string(message);

  // The message is copied to the sdk so the we can destroy it
  IoTHubMessage_Destroy(message_handle);
}

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
    uMQTT LORA FUNCTIONS
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

  if(verbose_flag){
    (void)printf("\r\nIncoming msg on topic: %s\r\n",
        mqttmessage_getTopicName(msgHandle)
    );
  }
  // parse incoming mqtt lora message as json
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
    const char *deveui = json_object_get_string(lora_json, "deveui");
    if(deveui!=NULL)(void)json_object_set_string(root_object, "deveui", deveui);

    const char *time_ = json_object_get_string(lora_json, "time");
		if(time_!=NULL)(void)json_object_set_string(root_object, "time", time_);

    const char *data = json_object_get_string(lora_json, "data");
    if(data!=NULL)(void)json_object_set_string(root_object, "data", data);
    const unsigned char* payload = BUFFER_u_char(Base64_Decoder(data));
    if(verbose_flag){
      (void)printf("Payload: %s\r\n", payload);
    }
    //send To iotHub
    sendToHub(json_serialize_to_string_pretty(root_value));

    json_value_free(root_value);
	}

	if(lora_value)json_value_free(lora_value);

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
            (void)printf("local mqtt ConnAck function called\r\n");

            SUBSCRIBE_PAYLOAD subscribe[1];
            subscribe[0].subscribeTopic = TOPIC_NAME;
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

/**
    Read config file
*/
static char *getConnectionString()
{
  char *result = NULL;
  FILE *cs_file;

  cs_file = fopen(conn_string_file, "r");
  if (cs_file == NULL)
  {
      printf("Error could not open config file for reading \r\n");
  }
  else
  {
      size_t file_size;

      (void)fseek(cs_file, 0, SEEK_END);
      file_size = ftell(cs_file);
      (void)fseek(cs_file, 0, SEEK_SET);
      // increment size to hold the null term
      file_size += 1;

      if (file_size == 0) // check wrap around
      {
          printf("Config file size invalid\r\n" );
      }
      else
      {
          result = (char*)calloc(file_size, 1);
          if (result == NULL)
          {
              printf("Could not allocate memory to hold the connection string\r\n");
          }
          else
          {
              // copy the file into the buffer
              size_t read_size = fread(result, 1, file_size - 1, cs_file);
              if (read_size != file_size - 1)
              {
                  printf("Error reading file\r\n ");
                  free(result);
                  result = NULL;
              }
          }
      }
      (void)fclose(cs_file);
  }

  (void)printf("Connection string: %s\r\n",result);
  return result;
}

/**
    -------------------------------------------------   MAIN
*/
int main(int argc, char **argv)
{
  static struct option long_options[] =
        {
          /* These options set a flag. */
          {"verbose", no_argument,       &verbose_flag, 1},
          {"brief",   no_argument,       &verbose_flag, 0},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"certs",  required_argument, 0, 'c'},
          {"config",    required_argument, 0, 's'},
          {0, 0, 0, 0}
        };
      /* getopt_long stores the option index here. */
  int c;
  int option_index = 0;

  while (c != -1){
      c = getopt_long (argc, argv, "abc:d:f:", long_options, &option_index);
     switch (c)
       {
       case 's':
        printf ("connection string file set to: `%s'\n", optarg);
        conn_string_file = optarg;
         break;
       case 'c':
          printf ("certs file set to: `%s'\n", optarg);
          edge_ca_cert_path = optarg;
         break;
       }
   }

  (void)printf("MULTITECH CONDUIT DOWSTREAM DEVICE V1.0 2019\r\n\r\n");

	IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol;
	protocol = MQTT_Protocol;

  char *cert_string = NULL;

  (void)IoTHub_Init(); // Used to initialize IoTHub SDK subsystem

 	(void)printf("Creating IoTHub handle\r\n");

  char *connectionString = getConnectionString(); //read connection string from config file
  // Get hubName from connection string to use as clientid in mqtt broker.
  char ** hubName = NULL;
  split(connectionString, '.', &hubName);
  printf("IoT Hub: %s\n", hubName[0]); // Get only first part.

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

        // Set constant interval for retry
        (void) IoTHubDeviceClient_SetRetryPolicy(device_handle, IOTHUB_CLIENT_RETRY_INTERVAL, 0);

        // Turn tracing on/off
        //bool traceOn = true;
        //(void)IoTHubDeviceClient_SetOption(device_handle, OPTION_LOG_TRACE, &traceOn);

        // Provide the Azure IoT device client with the Edge root
        // X509 CA certificate that was used to setup the Edge runtime
        cert_string = obtain_edge_ca_certificate();
        (void)IoTHubDeviceClient_SetOption(device_handle, OPTION_TRUSTED_CERT, cert_string);

        //Setting the auto URL Encoder (recommended for MQTT). Please use this option unless
        //you are URL Encoding inputs yourself.
        //ONLY valid for use with MQTT
        bool urlEncodeOn = true;
        (void)IoTHubDeviceClient_SetOption(device_handle, OPTION_AUTO_URL_ENCODE_DECODE, &urlEncodeOn);

        //twin retrieve
        (void)IoTHubDeviceClient_SetDeviceTwinCallback(device_handle, deviceTwinCallback, NULL);

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
            options.clientId = hubName[0]; //id from hub name.
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

  printf("\r\nPress any key to continue\r\n");
    (void)getchar();

 	return 0;
}
